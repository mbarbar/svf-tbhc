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
private:
    // undefined type == NULL.
    std::map<const NodeID, const Type *> idToTypeMap;
    // Maps an object ID to the location it was "born" from cloning.
    std::map<const NodeID, NodeID> idToCloneNodeMap;

protected:
    bool processAddr(const AddrSVFGNode* addr) override;
    bool processCopy(const CopySVFGNode* copy) override;

    // The following stay the same:
    //   processPhiNode.
    //   processLoad.
    //   processStore.

private:
    bool isCast(const CopySVFGNode *copy) const;

    bool isPod(const Type *t) const;
    // Returns true if a is a transitive base type of b, or a == b.
    bool isBase(const Type *a, const Type *b) const;
    // Returns pointee type of t.
    const Type *tilde(const Type *t) const;

    bool processCast(const CopySVFGNode *copy);
    bool processPodCast(const CopySVFGNode *copy);
    bool processFancyCast(const CopySVFGNode *copy);
};

#endif  // TYPECLONE_H_
