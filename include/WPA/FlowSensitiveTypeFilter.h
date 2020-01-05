//===- FlowSensitiveTypeFilter.h -- flow-sensitive type filter ----------------//

/*
 * FlowSensitiveTypeFilter.h
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

#ifndef FLOWSENSITIVETYPEFILTER_H_
#define FLOWSENSITIVETYPEFILTER_H_

#include "MemoryModel/DCHG.h"
#include "MSSA/SVFGOPT.h"
#include "MSSA/SVFGBuilder.h"
#include "WPA/FlowSensitive.h"
class SVFModule;

/*!
 * Flow sensitive whole program pointer analysis with type-based heap cloning.
 */
class FlowSensitiveTypeFilter : public FlowSensitive {
public:
    static const DIType *undefType;

    /// Constructor
    FlowSensitiveTypeFilter(PTATY type = FSTF_WPA) : FlowSensitive(type) {
    }

    /// Flow sensitive analysis with FSTF.
    virtual void analyze(SVFModule svfModule) override;
    /// Initialize analysis.
    virtual void initialize(SVFModule svfModule) override;
    /// Finalize analysis.
    virtual void finalize() override;

    /// Get PTA name
    virtual const std::string PTAName() const override{
        return "FSTF";
    }

    virtual bool propAlongIndirectEdge(const IndirectSVFGEdge* edge) override;
    virtual bool propAlongDirectEdge(const DirectSVFGEdge* edge) override;

    virtual bool processAddr(const AddrSVFGNode* addr) override;
    virtual bool processGep(const GepSVFGNode* gep) override;
    virtual bool processLoad(const LoadSVFGNode* load) override;
    virtual bool processStore(const StoreSVFGNode* store) override;
    virtual bool processPhi(const PHISVFGNode* phi) override;
    /// Initialise the pointees of ptrId (which is type tildet *). reuse indicates whether we allow reuse.
    virtual bool initialise(const SVFGNode *svfgNode, const NodeID ptrId, const DIType *tildet, bool reuse);

    /// Updates the PTS of pId to reflect changes (clones of what is in its current PTS)
    /// coming from the in set.
    virtual void preparePtsFromIn(const StmtSVFGNode *stmt, NodeID pId);

    /// Check if a node is a black hole obj or a constant object. We override to
    /// account for clones.
    inline virtual bool isBlkObjOrConstantObj(NodeID o) const override {
        if (isClone(o)) o = cloneToOriginalObj.at(o);
        return pag->isBlkObjOrConstantObj(o);
    }

private:
    /// Returns the ctir type attached to the value, nullptr if non-existant.
    /// Not static because it needs to DCHG to return the canonical type.
    const DIType *getTypeFromMetadata(const Value *) const;

    /// Returns a clone of o created at cloneSite with type type.
    NodeID cloneObject(NodeID o, const SVFGNode *cloneSite, const DIType *type);

    /// Wrapper around DCHGraph::isBase. Purpose is to keep our conditions clean
    /// by only passing two parameters like the rules.
    bool isBase(const llvm::DIType *a, const llvm::DIType *b) const;

    /// Returns true if o is a clone.
    bool isClone(NodeID o) const;

    /// Determines whether each GEP is a load or not. Builds gepIsLoad map.
    /// This is a quick heuristic; if all destination nodes are loads, it's a load.
    void determineWhichGepsAreLoads(void);

    /// Returns the GEP object node(s) of base for ls. This may include clones.
    /// If there are no GEP objects, then getGepObjNode is called on the PAG
    /// (through base's getGepObjNode) which will create one.
    std::set<NodeID> getGepObjClones(NodeID base, const LocationSet& ls);

    /// Object -> its type.
    /// undef type is TODO
    std::map<NodeID, const DIType *> objToType;
    /// Object -> allocation site (SVFG node).
    std::map<NodeID, NodeID> objToAllocation;
    /// (Original) object -> set of its clones.
    std::map<NodeID, std::set<NodeID>> objToClones;
    /// (Clone) object -> original object (opposite of objToclones).
    std::map<NodeID, NodeID> cloneToOriginalObj;
    /// Maps objects to the GEP nodes beneath them.
    std::map<NodeID, std::set<NodeID>> objToGeps;
    /// Maps GEP objects to the SVFG nodes that retrieved them with getGepObjClones.
    std::map<NodeID, std::set<NodeID>> gepToSVFGRetrievers;
    /// Maps whether a (SVFG) GEP node is a load or not.
    std::map<NodeID, bool> gepIsLoad;
    /// Maps memory objects to their GEP objects. (memobj -> (fieldidx -> geps))
    std::map<const MemObj *, std::map<unsigned, std::set<NodeID>>> memObjToGeps;
    /// Maps objects to the "lowest" possible type (through downcasts).
    std::map<NodeID, const DIType *> objToLowestType;

    DCHGraph *dchg = nullptr;
};

#endif /* FLOWSENSITIVETYPEFILTER_H_ */
