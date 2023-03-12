
#ifndef _REPLAYBUFFER_HH_
#define _REPLAYBUFFER_HH_

#include "mem/ruby/network/MessageBuffer.hh"
#include "params/ReplayBuffer.hh"


class ReplayBuffer : public MessageBuffer
{
public:
	typedef ReplayBufferParams Params;
    ReplayBuffer(const Params *p);
	//void retrieveMessage(MessageBuffer* pBuffer, Tick tick, Tick delta, int sequence);
	void retrieveMessage(MessageBuffer& pBuffer, Tick tick, Tick delta, int sequence);
	bool flushMessageUptoSeq(int sequence);
	void addMessage(MessageBuffer & pBuffer, Tick tick, Tick delta, int sequence);
	void removeMessage(int seq, Tick curTime);
	~ReplayBuffer();

};

#endif