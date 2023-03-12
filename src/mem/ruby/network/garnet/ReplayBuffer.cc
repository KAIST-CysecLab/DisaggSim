#include "mem/ruby/network/garnet/ReplayBuffer.hh"
#include <vector>
#include "mem/ruby/protocol/AckMsg.hh"

using namespace std;

ReplayBuffer::ReplayBuffer(const Params* p)
	: MessageBuffer((MessageBuffer::Params*)p)
{
}

/**
void ReplayBuffer::retrieveMessage(MessageBuffer* pBuffer, Tick tick, Tick delta, int sequence)
{
	MsgPtr res = NULL;
	vector<MsgPtr>::iterator i = m_prio_heap.begin();

	for (; i != m_prio_heap.end(); i++)
	{
		const Message* pMsg = (*i).get();
		const AckMsg* pAckMsg = dynamic_cast<const AckMsg*>(pMsg);
		if (pAckMsg && (pAckMsg->getseq() == sequence))
		{
			res = (*i);
			break;
		}
	}
	if (res != NULL)
		pBuffer->enqueue(res, tick, delta);
}
*/
void ReplayBuffer::retrieveMessage(MessageBuffer& pBuffer, Tick tick, Tick delta, int sequence)
{
	//retrieveMessage(pBuffer.getOrigPtr(), tick, delta, sequence);
	MsgPtr res = NULL;
	vector<MsgPtr>::iterator i = m_prio_heap.begin();

	for (; i != m_prio_heap.end(); i++)
	{
		Message* pMsg = (*i).get();
		if (pMsg && (pMsg->getsequence() == sequence))
		{
			res = (*i);
			break;
		}
	}
	if (res != NULL) {
		MsgPtr replay = res->clone();
		replay->setLastEnqueueTime(tick);
		pBuffer.enqueue(replay, tick, delta);

	}
}

void ReplayBuffer::addMessage(MessageBuffer & pBuffer, Tick tick, Tick delta, int sequence)
{
	MsgPtr res = NULL;

	res = pBuffer.peekMsgPtr()->clone();
	res->setLastEnqueueTime(tick);

	if (res != NULL && res->getsequence() == sequence)
		enqueue(res, tick, delta);
	else
		return;
}

void ReplayBuffer::removeMessage(int seq, Tick curTime)
{
	vector<MsgPtr>::iterator i = m_prio_heap.begin();

	for (; i != m_prio_heap.end(); i++)
	{
		Message* pMsg = (*i).get();

		// get the delay cycles
		pMsg->updateDelayedTicks(curTime);
		Tick delay = pMsg->getDelayedTicks();

		m_stall_time = curTick() - pMsg->getTime();

        // record previous size and time so the current buffer size
        // isn't adjusted until schd cycle
		if (m_time_last_time_pop < curTime) {
			m_size_at_cycle_start = m_prio_heap.size();
			m_stalled_at_cycle_start = m_stall_map_size;
			m_time_last_time_pop = curTime;
		}

		if (pMsg && (pMsg->getsequence() == seq))
		{
			m_prio_heap.erase(i);
			// TODO: set last dequeue tick to the buffer
			break;
		}
	}
}

ReplayBuffer::~ReplayBuffer()
{
	m_prio_heap.clear();
	this->clear();
}

bool ReplayBuffer::flushMessageUptoSeq(int sequence)
{
	bool res = false;
	vector<MsgPtr>::iterator i = m_prio_heap.begin();

	while (i != m_prio_heap.end())
	{
		Message* pMsg = (*i).get();
		if (pMsg && (pMsg->getsequence() <= sequence))
		{
			i = m_prio_heap.erase(i);
			res = true;
		}
		else
		{
			i++;
		}
	}

	return res;
}

ReplayBuffer*
ReplayBufferParams::create()
{
	return new ReplayBuffer(this);
}
