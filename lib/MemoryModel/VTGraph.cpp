//===- VTGraph.cpp -- Offline constraint graph -----------------------------//

/*
 * VTGraph.cpp
 *
 *  Created on: Nov 07, 2018
 *      Author: Mohamad Barbar
 */

#include "MemoryModel/VTGraph.h"
#include "Util/SVFUtil.h"
#include "Util/BasicTypes.h"
#include "MemoryModel/PAGNode.h"

llvm::cl::opt<bool> VTGDotGraph("dump-vtg", llvm::cl::init(false),
                                llvm::cl::desc("Dump dot graph of Vartiable Type Graph"));

const std::string VTGraph::CLASS_NAME_PREFIX = "class.";

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

void VTGraph::removeMemoryObjectNodes(void) {
    std::set<const FIObjPN*> fiObjNodes;
    for (auto nodeI = pag->begin(); nodeI != pag->end(); ++nodeI) {
        const PAGNode *pagNode = nodeI->second;
        if (const FIObjPN* fiObj = SVFUtil::dyn_cast<FIObjPN>(pagNode))
            fiObjNodes.insert(fiObj);
    }


    for (auto nodeI = fiObjNodes.begin(); nodeI != fiObjNodes.end(); ++nodeI) {
		const FIObjPN *fiObj = *nodeI;
		ConstraintNode *constraintNode = getConstraintNode(fiObj->getId());
		const Type *objType = fiObj->getMemObj()->getType();

        // Get the string representation of the type.
        std::string typeName;
        llvm::raw_string_ostream rso(typeName);
        objType->print(rso);
        typeName = rso.str();

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

		for (auto edgeI = addrs.begin(); edgeI != addrs.end(); edgeI++) {
			NodeID dstId = (*edgeI)->getDstID();
			removeAddrEdge(*edgeI);
			addAddrCGEdge(newSrcID, dstId);
		}
    }
}

void VTGraph::collapseFields(void) {
    std::set<NormalGepCGEdge *> gepEdges;
    ConstraintEdge::ConstraintEdgeSetTy &directEdges = getDirectCGEdges();
    for (auto edgeI = directEdges.begin(); edgeI != directEdges.end(); ++edgeI) {
        if (NormalGepCGEdge *gepEdge = SVFUtil::dyn_cast<NormalGepCGEdge>(*edgeI)) {
            gepEdges.insert(gepEdge);
        }
    }

    for (auto gepEdgeI = gepEdges.begin(); gepEdgeI != gepEdges.end(); ++gepEdgeI) {
        ConstraintNode *srcConstraintNode = (*gepEdgeI)->getSrcNode();
        ConstraintNode *dstConstraintNode = (*gepEdgeI)->getDstNode();

        NodeID srcId = srcConstraintNode->getId();
        NodeID dstId = dstConstraintNode->getId();

        PAGNode *srcPagNode = pag->getPAGNode(srcId);
        PAGNode *dstPagNode = pag->getPAGNode(dstId);

        NormalGepPE *pagGepEdge = static_cast<NormalGepPE *>(pag->getIntraPAGEdge(srcId, dstId, PAGEdge::NormalGep));
        u32_t offset = pagGepEdge->getOffset();

        // Does the src have a type? If not, keep moving backwards.
        const Type *srcType = srcPagNode->getType();
        if (srcType == NULL) {
            // TODO: what to do?
            assert("Cannot determine type of GEP accessor.");
        }

        // If it's not a StructType, getClassNameFromPointerType will handle it.
        const std::string className = getClassNameFromPointerType(srcType);
        if (chg->getNode(className) == NULL) continue;

    }
}

std::string VTGraph::getClassNameFromPointerType(const Type *type) {
    // Not given a pointer.
    if (!type->isPointerTy()) return "";

    const PointerType *ptrType = static_cast<const PointerType *>(type);
    while (ptrType->getContainedType(0)->isPointerTy()) {
        ptrType = static_cast<const PointerType *>(ptrType->getElementType());
    }

    // It's not a class.
    if (!ptrType->getElementType()->isStructTy()) {
        return "";
    }

    const StructType *st = static_cast<const StructType *>(ptrType->getContainedType(0));
    std::string name = st->getName();
    name.erase(0, VTGraph::CLASS_NAME_PREFIX.length());

    return name;
}


void VTGraph::dump(std::string name) {
    if (VTGDotGraph)
        GraphPrinter::WriteGraphToFile(SVFUtil::outs(), name, this);
}

namespace llvm {
    template<>
    struct DOTGraphTraits<VTGraph *> : public DOTGraphTraits<PAG*> {

        typedef ConstraintNode NodeType;
        DOTGraphTraits(bool isSimple = false) :
                DOTGraphTraits<PAG*>(isSimple) {
        }

        /// Return name of the graph
        static std::string getGraphName(VTGraph *graph) {
            return "Variable Type Graph";
        }

        /// Return label of a VFG node with two display mode
        /// Either you can choose to display the name of the value or the whole instruction
        static std::string getNodeLabel(NodeType *n, VTGraph*graph) {
            std::string str;
            raw_string_ostream rawstr(str);
            if (PAG::getPAG()->findPAGNode(n->getId())) {
                PAGNode *node = PAG::getPAG()->getPAGNode(n->getId());
                bool briefDisplay = true;
                bool nameDisplay = true;


                if (briefDisplay) {
                    if (SVFUtil::isa<ValPN>(node)) {
                        if (nameDisplay)
                            rawstr << node->getId() << ":" << node->getValueName();
                        else
                            rawstr << node->getId();
                    } else
                        rawstr << node->getId();
                } else {
                    // print the whole value
                    if (!SVFUtil::isa<DummyValPN>(node) && !SVFUtil::isa<DummyObjPN>(node))
                        rawstr << *node->getValue();
                    else
                        rawstr << "";

                }

                return rawstr.str();
            } else {
                rawstr<< n->getId();
                return rawstr.str();
            }
        }

        static std::string getNodeAttributes(NodeType *n, VTGraph *graph) {
            if (PAG::getPAG()->findPAGNode(n->getId())) {
                PAGNode *node = PAG::getPAG()->getPAGNode(n->getId());
                if (SVFUtil::isa<ValPN>(node)) {
                    if (SVFUtil::isa<GepValPN>(node))
                        return "shape=hexagon";
                    else if (SVFUtil::isa<DummyValPN>(node))
                        return "shape=diamond";
                    else
                        return "shape=circle";
                } else if (SVFUtil::isa<ObjPN>(node)) {
                    if (SVFUtil::isa<GepObjPN>(node))
                        return "shape=doubleoctagon";
                    else if (SVFUtil::isa<FIObjPN>(node))
                        return "shape=septagon";
                    else if (SVFUtil::isa<DummyObjPN>(node))
                        return "shape=Mcircle";
                    else
                        return "shape=doublecircle";
                } else if (SVFUtil::isa<RetPN>(node)) {
                    return "shape=Mrecord";
                } else if (SVFUtil::isa<VarArgPN>(node)) {
                    return "shape=octagon";
                } else {
                    assert(0 && "no such kind node!!");
                }
                return "";
            } else {
                return "shape=doublecircle";
            }
        }

        template<class EdgeIter>
        static std::string getEdgeAttributes(NodeType *node, EdgeIter EI, VTGraph *pag) {
            ConstraintEdge* edge = *(EI.getCurrent());
            assert(edge && "No edge found!!");
            if (edge->getEdgeKind() == ConstraintEdge::Addr) {
                return "color=green";
            } else if (edge->getEdgeKind() == ConstraintEdge::Copy) {
                return "color=black";
            } else if (edge->getEdgeKind() == ConstraintEdge::NormalGep
                       || edge->getEdgeKind() == ConstraintEdge::VariantGep) {
                return "color=purple";
            } else if (edge->getEdgeKind() == ConstraintEdge::Store) {
                return "color=blue";
            } else if (edge->getEdgeKind() == ConstraintEdge::Load) {
                return "color=red";
            } else {
                assert(0 && "No such kind edge!!");
            }
            return "";
        }

        template<class EdgeIter>
        static std::string getEdgeSourceLabel(NodeType *node, EdgeIter EI) {
            return "";
        }
    };
}

