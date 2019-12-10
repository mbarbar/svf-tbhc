//===- TypeBasedHeapCloning.h -- type-based Flow-sensitive heap cloning----------------//

/*
 * TypeBasedHeapCloning.h
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

#ifndef TYPEBASEDHEAPCLONING_H_
#define TYPEBASEDHEAPCLONING_H_

#include "MemoryModel/DCHG.h"
#include "MSSA/SVFGOPT.h"
#include "MSSA/SVFGBuilder.h"
#include "WPA/FlowSensitive.h"
class SVFModule;

/*!
 * Flow sensitive whole program pointer analysis with type-based heap cloning.
 */
class TypeBasedHeapCloning : public FlowSensitive {
public:
    static const DIType *undefType;

    /// Flow sensitive analysis with TBHC.
    virtual void analyze(SVFModule svfModule) override;
    /// Initialize analysis.
    virtual void initialize(SVFModule svfModule) override;
    /// Finalize analysis.
    virtual void finalize() override;

    /// Get PTA name
    virtual const std::string PTAName() const override{
        return "TBHC";
    }

    virtual bool processAddr(const AddrSVFGNode* addr) override;
    virtual bool processGep(const GepSVFGNode* edge) override;
    virtual bool processLoad(const LoadSVFGNode* load) override;
    virtual bool processStore(const StoreSVFGNode* store) override;
    virtual bool processDeref(const StmtSVFGNode *stmt, const NodeID ptrId);

private:
    /// Returns the ctir type attached to the value, nullptr if non-existant.
    /// Not static because it needs to DCHG to return the canonical type.
    const DIType *getTypeFromMetadata(const Value *) const;

    /// Returns the (one-level) pointee of ptrType.
    const DIType *tilde(const DIType *ptrType) const;

    /// Returns true if type represents some form of void.
    /// Currently, void itself (null), __vtbl_ptr_type, __internal_untyped.
    bool isVoid(const DIType *type) const;

    /// Returns a clone of o created at cloneSite with type type.
    NodeID cloneObject(NodeID o, const SVFGNode *cloneSite, const DIType *type);

    /// Wrapper around DCHGraph::isBase. Purpose is to keep our conditions clean
    /// by only passing two parameters like the rules.
    bool isBase(const llvm::DIType *a, const llvm::DIType *b) const;

    /// Returns true if o is a clone.
    bool isClone(NodeID o) const;

    /// Returns the GEP object node(s) of base for ls. This may include clones.
    /// If there are no GEP objects, then getGepObjNode is called on the PAG
    /// (through base's getGepObjNode) which will create one.
    std::set<NodeID> getGepObjClones(NodeID base, const LocationSet& ls);

    /// Object -> its type.
    /// undef type is TODO
    std::map<NodeID, const DIType *> objToType;
    /// Object -> allocation site (SVFG node).
    std::map<NodeID, NodeID> objToAllocation;
    /// Object -> cloning site (SVFG node).
    std::map<NodeID, NodeID> objToCloneSite;
    /// (Original) object -> set of its clones.
    std::map<NodeID, std::set<NodeID>> objToClones;
    /// (Clone) object -> original object (opposite of obj to clones).
    std::map<NodeID, NodeID> cloneToOriginalObj;
    /// Maps objects to the GEP nodes beneath them.
    std::map<NodeID, std::set<NodeID>> objToGeps;

    DCHGraph *dchg = nullptr;
};

#endif /* TYPEBASEDHEAPCLONING_H_ */
