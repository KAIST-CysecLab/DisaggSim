#include "disaggregated_component/translation_information.hh"

TranslationInformation::TranslationInformation(Addr pageSize):
    recvTable(pageSize, true),
    sendTable(pageSize, false)
{}

void
TranslationInformation::recvMap(
    Addr key,
    Addr value,
    size_t size
)
{
    recvTable.map(key, value, size, -1, -1, 1);
}

void
TranslationInformation::sendMap(
    Addr key,
    Addr value,
    size_t size,
    uint32_t cid,
    uint32_t subCID,
    uint32_t interleave_multiplier
)
{
    sendTable.map(key, value, size, cid, subCID, interleave_multiplier);
}



void TranslationInformation::recvTranslate(PacketPtr pkt)
{
    recvTable.translate(pkt);

}

void TranslationInformation::sendTranslate(PacketPtr pkt)
{
    sendTable.translate(pkt);
}

void
DisaggregatedTranslationTable::map(
    Addr key,
    Addr value,
    size_t size,
    uint32_t cid,
    uint32_t subCID,
    uint32_t interleave_multiplier
)
{
    // starting address must be page aligned
    assert(pageOffset(key) == 0);

    while (size > 0) {
        auto it = pTable.find(key);
        if (it != pTable.end()) {
            // if overlap overwrite
            it->second = Entry(value, cid, subCID);
        } else {
            pTable.emplace(key, Entry(value, cid, subCID));
        }

        size -= pageSize;
        key += pageSize * interleave_multiplier;
        value += pageSize * interleave_multiplier;
    }
}

void
DisaggregatedTranslationTable::unmap(Addr key, size_t size)
{
    assert(pageOffset(key) == 0);

    while (size > 0) {
        auto it = pTable.find(key);
        assert(it != pTable.end());
        pTable.erase(it);
        size -= pageSize;
        key += pageSize;
    }
}

const DisaggregatedTranslationTable::Entry *
DisaggregatedTranslationTable::lookup(Addr key)
{
    Addr page_addr = pageAlign(key);
    PTableItr iter = pTable.find(page_addr);
    if (iter == pTable.end())
        return nullptr;
    return &(iter->second);
}

bool
DisaggregatedTranslationTable::translate(
    Addr key,
    Addr &value,
    uint32_t& dstCID,
    uint32_t& dstSubCID
)
{
    const Entry *entry = lookup(key);
    if (!entry) {
        return false;
    }

    value = pageOffset(key) + entry->value;
    dstCID = entry->cid;
    dstSubCID = entry->subCID;

    return true;
}

void
DisaggregatedTranslationTable::translate(PacketPtr pkt)
{
    uint32_t dstCID = -1;
    uint32_t dstSubCID = -1;
    Addr daddr;
    RequestPtr req = pkt->req;

    if (isRecvTable){
        // if receive, only need to switch paddr since cids are already set (routed based on src cid)

        // translate req->_paddr
        if (!translate(req->getPaddr(), daddr, dstCID, dstSubCID))
            panic("RECV: no translation entry is provided");

        if ((daddr & (pageSize - 1)) + req->getSize() > pageSize) {
            panic("RECV: Request spans page boundaries!\n");
        }

        req->setPaddr(daddr);


        // translate pkt->addr
        if (!translate(pkt->getAddr(), daddr, dstCID, dstSubCID))
            panic("RECV: no translation entry is provided");

        if ((daddr & (pageSize - 1)) + req->getSize() > pageSize) {
            panic("RECV: Request spans page boundaries!\n");
        }

        pkt->setAddr(daddr);

    }else{
        // if send, need to switch dstcid and subcid

        // translate req->_paddr
        if (!translate(req->getPaddr(), daddr, dstCID, dstSubCID))
        {
            inform("%u, %u %u, %u, %u %s", pkt->getSrcCID(),req->getPaddr(), daddr, dstCID, dstSubCID, pkt->isResponse()?"true":"false");
            panic("SEND: no translation entry is provided");
        }

        if ((daddr & (pageSize - 1)) + req->getSize() > pageSize) {
            panic("SEND: Request spans page boundaries!\n");
        }

        req->setPaddr(daddr);
        req->setDstCID(dstCID);
        req->setMemSubCID(dstSubCID);

        // translate pkt->addr
        if (!translate(pkt->getAddr(), daddr, dstCID, dstSubCID))
            panic("RECV: no translation entry is provided");

        if ((daddr & (pageSize - 1)) + req->getSize() > pageSize) {
            panic("RECV: Request spans page boundaries!\n");
        }

        pkt->setAddr(daddr);

    }

}
