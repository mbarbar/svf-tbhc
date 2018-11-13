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

std::string edgeKindName(ConstraintEdge::GEdgeKind ek) {
    if (ek == ConstraintEdge::Addr)
        return "Addr";
    else if (ek == ConstraintEdge::Copy)
        return "Copy";
    else if (ek == ConstraintEdge::Store)
        return "Store";
    else if (ek == ConstraintEdge::Load)
        return "Load";
    else if (ek == ConstraintEdge::NormalGep)
        return "NormalGep";
    else if (ek == ConstraintEdge::VariantGep)
        return "VariantGep";
}

std::set<NodeID> VTAGraph::getFIObjectNodes(void) {
    std::set<NodeID> objNodes;

    for (auto nodeI = begin(); nodeI != end(); ++nodeI) {
        NodeID nodeId = nodeI->first;
        PAGNode *pagNode = pag->getPAGNode(nodeId);

        if (pagNode->getNodeKind() == PAGNode::FIObjNode) {
            objNodes.insert(nodeId);
        }
    }

    return objNodes;
}

void VTAGraph::removeMemoryObjectNodes(void) {
    std::set<const FIObjPN*> objNodes;
    for (auto nodeI = pag->begin(); nodeI != pag->end(); ++nodeI) {
        const PAGNode *pagNode = nodeI->second;
        if (const FIObjPN* fiObj = SVFUtil::dyn_cast<FIObjPN>(pagNode))
            objNodes.insert(fiObj);
    }


    for (auto nodeI = objNodes.begin(); nodeI != objNodes.end(); ++nodeI) {
		const FIObjPN *fiObj = *nodeI;
		ConstraintNode *constraintNode = getConstraintNode(fiObj->getId());
		const Type *objType = fiObj->getMemObj()->getType();

        // Get the string representation of the type.
        std::string typeName;
        llvm::raw_string_ostream rso(typeName);
        objType->print(rso);
        typeName = rso.str();

        llvm::outs() << "type: " << objType << "\n";
        // TODO: this is unreliable. Case: %"class ...
        if (typeName.compare(0, std::string("%class").size(), "%class") != 0) continue;

        NodeID newSrcID;

        if (typeToNode.count(objType) != 0) {
            // A type node was created.
            newSrcID = typeToNode.at(objType);
        } else {
            // No type node for objType, so make one.
            newSrcID = pag->addDummyTypeObjNode(objType);
            addConstraintNode(new ConstraintNode(newSrcID), newSrcID);
            typeToNode[objType] = newSrcID;
        }

        std::set<AddrCGEdge*> addrs;
		for (auto edgeI = constraintNode->getOutEdges().begin(); edgeI != constraintNode->getOutEdges().end(); ++edgeI) {
			if (AddrCGEdge *addrEdge = SVFUtil::dyn_cast<AddrCGEdge>(*edgeI))
				addrs.insert(addrEdge);
			assert(addrs.size() == 1 && "an object does not have one outgoing address edge?");
		}
		for(auto edgeI = addrs.begin(); edgeI != addrs.end(); edgeI++){
			NodeID dstId = (*edgeI)->getDstID();
			removeAddrEdge(*edgeI);
			addAddrCGEdge(newSrcID, dstId);
		}
    }
}

