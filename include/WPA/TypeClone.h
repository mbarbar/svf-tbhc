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

protected:
    bool processAddr(const AddrSVFGNode* addr) override;
    bool processCopy(const CopySVFGNode* copy) override;

    // The following stay the same:
    //   processPhiNode.
    //   processLoad.
    //   processStore.

private:
    bool isCast(const CopySVFGNode *copy, Type **fromType, Type **toType) const;
};

#endif  // TYPECLONE_H_
