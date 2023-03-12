#ifndef __DISAGREGATED_COMPONENT_MAPPING_TABLE_HH__
#define __DISAGREGATED_COMPONENT_MAPPING_TABLE_HH__

#include <unordered_map>

#include "base/intmath.hh"
#include "base/types.hh"
#include "mem/port.hh"
#include "mem/request.hh"

class DisaggregatedTranslationTable
{
  public:
    struct Entry
    {
        Addr value;
        // for sending request
        // if recieving request, set as the owning module's cid
        uint32_t cid;
        uint32_t subCID;
        Entry(Addr addr, uint32_t cid, uint32_t subCID) : value(addr), cid(cid), subCID(subCID) {}
        Entry() {}
    };

  protected:
    typedef std::unordered_map<Addr, Entry> PTable;
    typedef PTable::iterator PTableItr;
    PTable pTable;

    const Addr pageSize;
    const Addr offsetMask;

    bool isRecvTable;


  public:

    DisaggregatedTranslationTable(Addr _pageSize, bool isRecvTable) :
        pageSize(_pageSize),
        offsetMask(mask(floorLog2(_pageSize))),
        isRecvTable(isRecvTable)
    {
        assert(isPowerOf2(pageSize));
    }


    Addr pageAlign(Addr a)  { return (a & ~offsetMask); }
    Addr pageOffset(Addr a) { return (a &  offsetMask); }

    /**
     * Maps a memory region.
     * @param vaddr The address to be translated.
     * @param paddr The translated address.
     * @param size The length of the region.
     * @param cid Target CID
     * @param interleave_multiplier Interleaving multiplier (4KB unit)
     */
    void map(Addr key, Addr value, size_t size, uint32_t cid, uint32_t subCID, uint32_t interleave_multiplier);
    void unmap(Addr vaddr, size_t size);


    /**
     * Lookup function
     * @param key The address to be translated.
     * @return The page table entry corresponding to vaddr.
     */
    const Entry *lookup(Addr key);

    /**
     * Translate function
     * @param key The address to be translated.
     * @param value The translated address.
     * @return True if translation exists
     */
    bool translate(Addr key, Addr &value, uint32_t& dstCID, uint32_t& subCID);

    /**
     * Perform a translation on the memory traffic, translate pkt->addr and pkt->req->_paddr
     * field of req.
     * @param pkt The memory packet.
     */
    void translate(PacketPtr pkt);

};


class TranslationInformation
{
    private:
        // disaggregated address to phyiscal address translation
        DisaggregatedTranslationTable recvTable;
        // phyiscal address to disaggregated address translation
        DisaggregatedTranslationTable sendTable;

    public:

        TranslationInformation(Addr pageSize);

        void recvMap(Addr key, Addr value, size_t size);
        void sendMap(Addr key, Addr value, size_t size, uint32_t cid, uint32_t subCID, uint32_t interleave_multiplier=1);

        void recvTranslate(PacketPtr pkt);
        void sendTranslate(PacketPtr pkt);
};



#endif
