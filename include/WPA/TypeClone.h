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
    // undefined type == NULL.
    std::map<const NodeID, const Type *> idToTypeMap;

    bool processAddr(const AddrSVFGNode* addr) override;

    // The following stay the same:
    //   processPhiNode.
};

#endif  // TYPECLONE_H_
