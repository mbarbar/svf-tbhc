//===- FlowSensitiveTBHC.h -- flow-sensitive type filter ----------------//

/*
 * FlowSensitiveTBHC.h
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

#ifndef FLOWSENSITIVETYPEFILTER_H_
#define FLOWSENSITIVETYPEFILTER_H_

#include "MemoryModel/DCHG.h"
#include "MSSA/SVFGOPT.h"
#include "MSSA/SVFGBuilder.h"
#include "Util/TypeBasedHeapCloning.h"
#include "WPA/FlowSensitive.h"
class SVFModule;

/*!
 * Flow sensitive whole program pointer analysis with type-based heap cloning.
 */
class FlowSensitiveTBHC : public FlowSensitive, public TypeBasedHeapCloning {
public:
    /// Returns raw ctir metadata of the instruction behind a SVFG node.
    /// Wraps getRawCTirMetadata(const Value *). Returns null if it doesn't exist.
    static const MDNode *getRawCTirMetadata(const SVFGNode *);

    /// Constructor
    FlowSensitiveTBHC(PTATY type = FSTBHC_WPA);

    /// Flow sensitive analysis with FSTBHC.
    virtual void analyze(SVFModule svfModule) override;
    /// Initialize analysis.
    virtual void initialize(SVFModule svfModule) override;
    /// Finalize analysis.
    virtual void finalize() override;

    /// Get PTA name
    virtual const std::string PTAName() const override{
        return "FSTBHC";
    }

    virtual bool propAlongIndirectEdge(const IndirectSVFGEdge* edge) override;
    virtual bool propAlongDirectEdge(const DirectSVFGEdge* edge) override;

    virtual bool processAddr(const AddrSVFGNode* addr) override;
    virtual bool processGep(const GepSVFGNode* gep) override;
    virtual bool processLoad(const LoadSVFGNode* load) override;
    virtual bool processStore(const StoreSVFGNode* store) override;
    virtual bool processPhi(const PHISVFGNode* phi) override;
    virtual bool processCopy(const CopySVFGNode* copy) override;

    virtual inline const NodeBS& getAllFieldsObjNode(NodeID id) override;

    /// Updates the PTS of pId to reflect changes (clones of what is in its current PTS)
    /// coming from the in set.
    virtual void preparePtsFromIn(const StmtSVFGNode *stmt, NodeID pId);

    /// Extracts the value from SVFGNode (if it exists), and calls
    /// getTypeFromCTirMetadata(const Value *).
    /// If no ctir type exists, returns null (void).
    const DIType *getTypeFromCTirMetadata(const SVFGNode *);

protected:
    virtual void backPropagate(NodeID clone) override;

    virtual void countAliases(std::set<std::pair<NodeID, NodeID>> cmp, unsigned *mayAliases, unsigned *noAliases) override;

private:
    /// Determines whether each GEP is a load or not. Builds gepIsLoad map.
    /// This is a quick heuristic; if all destination nodes are loads, it's a load.
    void determineWhichGepsAreLoads(void);

    /// Maps GEP objects to the SVFG nodes that retrieved them with getGepObjClones.
    std::map<NodeID, NodeBS> gepToSVFGRetrievers;
    /// Maps whether a (SVFG) GEP node is a load or not.
    std::map<NodeID, bool> gepIsLoad;
};

#endif /* FLOWSENSITIVETYPEFILTER_H_ */