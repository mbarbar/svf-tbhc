
 * TypeClone.h
 *
 *  Created on: Apr 09, 2019
 *      Author: Mohamad Barbar
 */

#ifndef TYPECLONE_H_
#define TYPECLONE_H_

#include "WPA/FlowSensitive.h"

class TypeClone : public FlowSensitive {
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

    bool processGep(const GepSVFGNode* edge) override;
    bool processLoad(const LoadSVFGNode* load) override;
    bool processStore(const StoreSVFGNode* store) override;
    // TODO: bool return necessary?
    virtual bool processDeref(const SVFGNode *deref);

    virtual void backPropagate(NodeID o);


    virtual void initialize(SVFModule svfModule) override;

private:

    // Returns true if a is a transitive base type of b, or a == b.
    bool isBase(TypeStr a, TypeStr b) const;
    // Returns pointee type of t.
    TypeStr tilde(TypeStr t) const;
};

#endif  // TYPECLONE_H_
