//===- TypeBasedHeapCloning.h -- type-based Flow-sensitive heap cloning----------------//

/*
 * TypeBasedHeapCloning.h
 *
 *  Created on: Oct 08, 2019
 *      Author: Mohamad Barbar
 */

#ifndef TYPEBASEDHEAPCLONING_H_
#define TYPEBASEDHEAPCLONING_H_

#include "MSSA/SVFGOPT.h"
#include "MSSA/SVFGBuilder.h"
#include "WPA/FlowSensitive.h"
class SVFModule;

/*!
 * Flow sensitive whole program pointer analysis with type-based heap cloning.
 */
class TypeBasedHeapCloning : public FlowSensitive {
public:
    static const llvm::DIType *undefType;

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

private:
    /// Returns the tir type attached to the value, nullptr if non-existant.
    const llvm::DIType *getTypeFromMetadata(const Value *) const;

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
};

#endif /* TYPEBASEDHEAPCLONING_H_ */
