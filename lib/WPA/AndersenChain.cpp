//===- AndersenChain.cpp -- Andersen's analysis with chain dereferencing --//

/*
 * AndersenChain.cpp
 *
 *  Created on: 05/02/2019
 *      Author: Mohamad Barbar
 */

#include "Util/SVFUtil.h"
#include "WPA/Andersen.h"

const unsigned AndersenChain::maxDerefValue = 20;

NodeID AndersenChain::getChainId(NodeID origId) {
    NodeID chainId = origId;
    if (origToChain.find(origId) != origToChain.end()) {
        chainId = origToChain.at(origId);
    }

    return chainId;
}

NodeID AndersenChain::getNextChainId(NodeID chainId) {
    ChainObjPN *chainObj = SVFUtil::dyn_cast<ChainObjPN>(pag->getPAGNode(chainId));
    assert(chainObj && "Trying to get next chain object of non-chain object");

    ChainObjPN *nextChainObj = chainObj->getNextChainObj();
    NodeID nextChainId;
    if (nextChainObj == NULL) {
        // Chain is getting too long - loop?
        if (chainObj->getDerefLevel() > maxDerefValue) {
            return 0;
        }

        // Doesn't have a next chain, so create one.
        nextChainId = pag->addDummyChainObjNode(chainObj->getDerefLevel() + 1);
        consCG->addConstraintNode(new ConstraintNode(nextChainId), nextChainId);
        // Needs to be able to propagate itself.
        getPts(nextChainId).set(nextChainId);

        ChainObjPN *nextChainObj = SVFUtil::dyn_cast<ChainObjPN>(pag->getPAGNode(nextChainId));
        chainObj->setNextChainObj(nextChainObj);
    } else {
        nextChainId = nextChainObj->getId();
    }

    return nextChainId;
}

bool AndersenChain::processLoad(NodeID node, const ConstraintEdge* load) {
    // return Andersen::processLoad(node, load);
    if (pag->isConstantObj(node) || isNonPointerObj(node))
        return false;

    numOfProcessedLoad++;

    NodeID dst = load->getDstID();

    NodeID nextChainId = getNextChainId(node);
    // Probably a loop.
    if (nextChainId == 0) return false;

    // We do our own worklist stuff because we use modified nodes.
    if (addCopyEdge(nextChainId, dst)) {
        pushIntoWorklist(nextChainId);
    }

    return false;
}

bool AndersenChain::processStore(NodeID node, const ConstraintEdge* store) {
    // return Andersen::processStore(node, store);
    if (pag->isConstantObj(node) || isNonPointerObj(node))
        return false;

    numOfProcessedStore++;

    NodeID src = store->getSrcID();

    NodeID nextChainId = getNextChainId(node);
    // Probably a loop.
    if (nextChainId == 0) return false;
    ChainObjPN *nextChainObj = SVFUtil::dyn_cast<ChainObjPN>(pag->getPAGNode(nextChainId));

    // nextChainObj's PTS has everything src's PTS does and will have.
    nextChainObj->getPtsNodes().set(src);

    return false;
}

void AndersenChain::convertFIObjectsToChainObjects(void) {
    ConstraintEdge::ConstraintEdgeSetTy addrEdges;
    addrEdges.insert(consCG->getAddrCGEdges().begin(), consCG->getAddrCGEdges().end());

    for (ConstraintEdge::ConstraintEdgeSetTy::iterator addrI = addrEdges.begin(); addrI != addrEdges.end(); ++addrI) {
        AddrCGEdge *addr = SVFUtil::dyn_cast<AddrCGEdge>(*addrI);

        // Make a new chain object.
        NodeID chainId = pag->addDummyChainObjNode(0);
        ConstraintNode *chainConstraintNode = new ConstraintNode(chainId);
        consCG->addConstraintNode(chainConstraintNode, chainId);

        // Save the mapping.
        NodeID origId = addr->getSrcID();
        chainToOrig[chainId] = origId;
        origToChain[origId] = chainId;

        // Detach and reattach the addr edge.
        NodeID oldDstId = addr->getDstID();
        consCG->removeAddrEdge(addr);
        consCG->addAddrCGEdge(chainId, oldDstId);
    }
}

