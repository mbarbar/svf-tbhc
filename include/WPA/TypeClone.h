/*
 * TypeClone.h
 *
 *  Created on: Apr 09, 2019
 *      Author: Mohamad Barbar
 */

#ifndef TYPECLONE_H_
#define TYPECLONE_H_

#include "WPA/FlowSensitive.h"

class TypeClone : public FlowSensitive {
public:
    typedef std::string TypeStr;

    static const std::string UNDEF_TYPE;

private:
    SVFModule svfModule;
    CHGraph *chg = PointerAnalysis::getCHGraph();

    // undefined type == "".
    std::map<const NodeID, TypeStr> idToTypeMap;
    // Maps an object ID to the location it was "born" from cloning.
    std::map<const NodeID, NodeID> idToCloneLocMap;
    // Maps an object ID to the location it was *actually* allocated at.
    std::map<const NodeID, NodeID> idToAllocLocMap;

    // Maps a standard object to all the clones made from it.
    // pagObjectId -> (svfgId -> pagObjId)
    // i.e. untyped object gives you a map which maps initialisation points to
    // clone made at that initialisation point.
    std::map<const NodeID, std::map<NodeID, NodeID>> idToClonesMap;

    std::map<NodeID, NodeID> cloneToBaseMap;

protected:
    bool processAddr(const AddrSVFGNode* addr) override;

    bool processGep(const GepSVFGNode* edge) override;
    bool processLoad(const LoadSVFGNode* load) override;
    bool processStore(const StoreSVFGNode* store) override;

    virtual bool processDeref(const SVFGNode *stmt, const NodeID ptrId);

    virtual bool baseBackPropagate(NodeID o);


    virtual void initialize(SVFModule svfModule) override;

private:
    NodeID clone(const NodeID o, SVFGNode *cloneLoc, TypeStr type);
    NodeID getCloneObject(const NodeID o, SVFGNode *cloneLoc) const;

    // Returns true if a is a transitive base type of b, or a == b.
    bool isBase(TypeStr a, TypeStr b) const;
    // Returns pointee type of t.
    TypeStr tilde(TypeStr t) const;

    TypeStr T(NodeID n) const;

    /// Returns the static type of a pointer.
    TypeStr staticType(NodeID p) const;
};

#endif  // TYPECLONE_H_
