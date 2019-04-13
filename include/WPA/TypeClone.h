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
    static const std::string UNDEF_TYPE;
    typedef std::string TypeStr;

private:
    SVFModule svfModule;
    CHGraph *chg = PointerAnalysis::getCHGraph();

    // undefined type == "".
    std::map<const NodeID, TypeStr> idToTypeMap;
    // Maps an object ID to the location it was "born" from cloning.
    std::map<const NodeID, NodeID> idToCloneNodeMap;
    // Maps an object ID to the location it was *actually* allocated at.
    std::map<const NodeID, NodeID> idToAllocNodeMap;

protected:
    bool processAddr(const AddrSVFGNode* addr) override;
    bool processCopy(const CopySVFGNode* copy) override;
    void initialize(SVFModule svfModule) override;

    // The following stay the same:
    //   processPhiNode.
    //   processLoad.
    //   processStore.

private:
    bool isCast(const CopySVFGNode *copy) const;

    bool isPod(TypeStr t) const;
    // Returns true if a is a transitive base type of b, or a == b.
    bool isBase(TypeStr a, TypeStr b) const;
    // Returns pointee type of t.
    TypeStr tilde(TypeStr t) const;

    bool processCast(const CopySVFGNode *copy);
    bool processPodCast(const CopySVFGNode *copy);
    bool processFancyCast(const CopySVFGNode *copy);
};

#endif  // TYPECLONE_H_
