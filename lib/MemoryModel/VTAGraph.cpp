//===- VTAGraph.cpp -- Offline constraint graph -----------------------------//

/*
 * VTAGraph.cpp
 *
 *  Created on: Nov 07, 2018
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/VTAGraph.h"
#include "Util/SVFUtil.h"
#include "Util/BasicTypes.h"
#include "MemoryModel/PAGNode.h"

std::string nodeKindName(GenericNode<PAGNode, PAGEdge>::GNodeK nk) {
    if (nk == PAGNode::ValNode)
        return "ValNode";
    else if (nk == PAGNode::ObjNode)
        return "ObjNode";
    else if(nk == PAGNode::RetNode)
        return "RetNode";
    else if(nk == PAGNode::VarargNode)
        return "VarargNode";
    else if(nk == PAGNode::GepValNode)
        return "GepValNode";
    else if(nk == PAGNode::GepObjNode)
        return "GepObjNode";
    else if(nk == PAGNode::FIObjNode)
        return "FIObjNode";
    else if(nk == PAGNode::DummyValNode)
        return "DummyValNode";
    else if(nk == PAGNode::DummyObjNode)
        return "DummyObjNode";
    else if(nk == PAGNode::TypeObjNode)
        return "TypeObjNode";
}

void VTAGraph::removeMemoryObjectNodes(void) {
    llvm::outs() << "removeMemoryObjectNodes\n";
    for (auto nodeI = begin(); nodeI != end(); ++nodeI) {
        NodeID nodeId = nodeI->first;
        PAGNode *pagNode = pag->getPAGNode(nodeId);
        ConstraintNode *constraintNode = getConstraintNode(nodeId);
        /*
        llvm::outs() << "NodeID: " << nodeId << " - "
                     << "kind: "   << nodeKindName(pagNode->getNodeKind()) << "\n";
        llvm::outs() << pagNode << "\n";
        */
        if (!ObjPN::classof(pagNode)) continue;
        if (pagNode->getNodeKind() != PAGNode::FIObjNode) continue;

        ObjPN *objNode = static_cast<FIObjPN *>(pagNode);
        Type *objType = objNode->getMemObj()->getTypeInfo()->getLLVMType();

        std::string typeName;
        llvm::raw_string_ostream rso(typeName);
        objType->print(rso);
        typeName = rso.str();

        // TODO: this is unreliable. Case: %"class ...
        if (typeName.compare(0, std::string("%class").size(), "%class") != 0) continue;

        NodeID newSrcID;

        llvm::outs() << typeToNode.count(objType) << "\n";
        if (typeToNode.count(objType) != 0) {
            newSrcID = typeToNode.at(objType);
            llvm::outs() << "Type: " << *objType
                         << "newSrcID: " << newSrcID
                         << " - IF\n";
        } else {
            newSrcID = pag->addDummyTypeObjNode(objType);
            llvm::outs() << "newSrcID is " << newSrcID << "\n";
            addConstraintNode(new ConstraintNode(newSrcID), newSrcID);
            typeToNode[objType] = newSrcID;
            llvm::outs() << "Type: " << *objType
                         << "newSrcID: " << newSrcID
                         << " - ELSE\n";
        }

        for (auto edgeI = constraintNode->getOutEdges().begin();
             edgeI != constraintNode->getOutEdges().end();
             ++edgeI) {
            if ((*edgeI)->getEdgeKind() != PAGEdge::Addr) continue;
            AddrCGEdge *addrEdge = static_cast<AddrCGEdge *>(*edgeI);
            // Keep the old dst, attach it to the new src.
            NodeID dstId = addrEdge->getDstID();
            addAddrCGEdge(newSrcID, dstId);
            removeAddrEdge(addrEdge);
        }

        llvm::outs() << "Mem: " << *(objNode->getMemObj()->getRefVal()->getType()) << "\n";
        if (objType == NULL) {
            llvm::outs() << "Type is null\n";
        } else {
            llvm::outs() << "NodeID: " << nodeId
                         << "  Type: " << *objType
                         << "  Kind: " << nodeKindName(pagNode->getNodeKind())
                         << " struct type? " << objNode->getMemObj()->getRefVal()->getName()
                         << "\n";
        }
    }
}

