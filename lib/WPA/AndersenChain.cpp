//===- AndersenChain.cpp -- Andersen's analysis with chain dereferencing --//

/*
 * AndersenChain.cpp
 *
 *  Created on: 05/02/2019
 *      Author: Mohamad Barbar
 */

#include "Util/SVFUtil.h"
#include "WPA/Andersen.h"

void AndersenChain::convertFIObjectsToChainObjects(void) {
    ConstraintEdge::ConstraintEdgeSetTy& addrEdges = consCG->getAddrCGEdges();
    for (ConstraintEdge::ConstraintEdgeSetTy::iterator addrI = addrEdges.begin(); addrI != addrEdges.end(); ++addrI) {
        AddrCGEdge *addr = SVFUtil::dyn_cast<AddrCGEdge>(*addrI);

        // Make a new chain object.
        NodeID chainId = pag->addDummyChainObjNode(0);
        ConstraintNode *chainConstraintNode = new ConstraintNode(chainId);
        consCG->addConstraintNode(chainConstraintNode, chainId);

        // Save the mapping.
        NodeID origId = addr->getSrcID();
        chainToOrig[chainId] = origId;

        // Detach and reattach the addr edge.
        NodeID oldDstId = addr->getDstID();
        consCG->removeAddrEdge(addr);
        consCG->addAddrCGEdge(chainId, oldDstId);
    }
}

