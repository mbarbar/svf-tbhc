//===- VTGraph.cpp -- Offline constraint graph -----------------------------//

/*
 * VTGraph.cpp
 *
 *  Created on: Nov 07, 2018
 *      Author: Mohamad Barbar
 */

#include <tuple>

#include "MemoryModel/VTGraph.h"
#include "Util/SVFUtil.h"
#include "Util/BasicTypes.h"
#include "Util/CPPUtil.h"
#include "MemoryModel/PAGNode.h"
#include "MemoryModel/CHA.h"

llvm::cl::opt<bool> VTGDotGraph("dump-vtg", llvm::cl::init(false),
                                llvm::cl::desc("Dump dot graph of Vartiable Type Graph"));

const std::string VTGraph::CLASS_NAME_PREFIX = "class.";

void VTGraph::collapseMemoryObjectsIntoTypeObjects(void) {
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

        if (!hasNonScalarTypes(objType)) {
            // Detach and continue - don't propagate these.
            std::set<AddrCGEdge*> addrs;
            for (auto edgeI = constraintNode->getOutEdges().begin(); edgeI != constraintNode->getOutEdges().end(); ++edgeI) {
                if (AddrCGEdge *addrEdge = SVFUtil::dyn_cast<AddrCGEdge>(*edgeI))
                    addrs.insert(addrEdge);
            }

            assert(addrs.size() == 1 && "object has more/less than 1 outgoing addr edge");
            for (auto edgeI = addrs.begin(); edgeI != addrs.end(); edgeI++) {
                removeAddrEdge(*edgeI);
            }

            removeConstraintNode(constraintNode);

            continue;
        }


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

        // Collect the addr edge for removal.
        std::set<AddrCGEdge*> addrs;
        for (auto edgeI = constraintNode->getOutEdges().begin(); edgeI != constraintNode->getOutEdges().end(); ++edgeI) {
            if (AddrCGEdge *addrEdge = SVFUtil::dyn_cast<AddrCGEdge>(*edgeI))
                addrs.insert(addrEdge);
        }

        assert(addrs.size() == 1 && "object has more/less than 1 outgoing addr edge");

        // Remove the addr edge and add the new one (typeObj->oldDst).
        for (auto edgeI = addrs.begin(); edgeI != addrs.end(); edgeI++) {
            NodeID dstId = (*edgeI)->getDstID();
            removeAddrEdge(*edgeI);
            addAddrCGEdge(newSrcID, dstId);
        }

        // Remove the old constraintNode
        removeConstraintNode(constraintNode);
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
        NodeID srcId = (*gepEdgeI)->getSrcID();
        NodeID dstId = (*gepEdgeI)->getDstID();

        PAGNode *srcPagNode = pag->getPAGNode(srcId);

        NormalGepPE *pagGepEdge = SVFUtil::dyn_cast<NormalGepPE>(pag->getIntraPAGEdge(srcId, dstId, PAGEdge::NormalGep));
        u32_t offset = pagGepEdge->getOffset();

        // Does the src have a type?
        const Type *srcType = srcPagNode->getType();
        if (srcType == NULL) continue;

        // If it's not a StructType, getClassNameFromPointerType will handle it.
        const std::string accessorClass = getClassNameFromType(srcType);
        if (chg->getNode(accessorClass) == NULL) continue;

        std::tuple<std::string, u32_t> fieldKey = std::tuple<std::string, u32_t>(accessorClass, offset);
        if (fieldRepresentationMap.count(fieldKey) == 0) {
            // Make the gep node the field representation, nothing more to do.
            fieldRepresentationMap[fieldKey] = dstId;
        } else {
            // Use the existing one.
            NodeID fieldRepresentationNodeId = fieldRepresentationMap.at(fieldKey);

            // Detatch from the actual dst, attach to the representation.
            removeDirectEdge(*gepEdgeI);
            addNormalGepCGEdge(srcId, fieldRepresentationNodeId, (*gepEdgeI)->getLocationSet());
        }
    }
}

std::string VTGraph::getClassNameFromType(const Type *type) {
    if (const StructType *structType = SVFUtil::dyn_cast<StructType>(type)) {
        return getClassNameFromStructType(structType);
    }

    if (const PointerType *pointerType = SVFUtil::dyn_cast<PointerType>(type)) {
        return getClassNameFromPointerType(pointerType);
    }

    return "???";
}

std::string VTGraph::getClassNameFromStructType(const StructType *structType) {
    if (!structType->hasName()) return "";
    std::string name = structType->getName();
    name.erase(0, VTGraph::CLASS_NAME_PREFIX.length());
    return name;
}

std::string VTGraph::getClassNameFromPointerType(const Type *type) {
    // Not given a pointer.
    if (!type->isPointerTy()) return "";

    const PointerType *ptrType = SVFUtil::dyn_cast<const PointerType>(type);
    while (ptrType->getContainedType(0)->isPointerTy()) {
        ptrType = SVFUtil::dyn_cast<const PointerType>(ptrType->getElementType());
    }

    // It's not a class.
    if (!ptrType->getElementType()->isStructTy()) {
        return "";
    }

    const StructType *st = SVFUtil::dyn_cast<const StructType>(ptrType->getContainedType(0));
    std::string name = "";
    if (st->hasName()) name = st->getName();
    name.erase(0, VTGraph::CLASS_NAME_PREFIX.length());

    return name;
}

const Type *VTGraph::dereferencePointerType(const PointerType *pt) {
    while (pt->getElementType()->isPointerTy()) {
        pt = SVFUtil::dyn_cast<const PointerType>(pt->getElementType());
    }

    return pt->getElementType();
}

std::string VTGraph::getFieldDeclarer(std::string accessingClass, const StructType *accessingType, u32_t fieldOffset) {
    // We will walk up the parents
    std::string declarer = "";

    SymbolTableInfo *symInfo = SymbolTableInfo::Symbolnfo();

    CHNode *chNode = chg->getNode(accessingClass);
    CHGraph::CHNodeSetTy ancestors = chg->getAncestors(accessingClass);

    std::string currName = declarer;

    const StructType *containingType = accessingType;
    bool done = false;
    while (true) {
        StInfo *si = symInfo->getStructInfo(containingType);
        std::vector<u32_t> fieldOffsets = si->getFieldIdxVec();

        for (auto fieldOffsetI = fieldOffsets.begin(); fieldOffsetI != fieldOffsets.end(); ++fieldOffsetI) {
            u32_t currFieldOffset = *fieldOffsetI;
            const Type *currFieldType = si->getFieldTypeWithFldIdx(currFieldOffset);

            if (currFieldOffset == fieldOffset) {
                // If the next offset belongs to this type, then we found what
                // we needed, and not to a parent type.
                auto nextI = fieldOffsetI + 1;
                if (nextI == fieldOffsets.end() || *nextI == currFieldOffset + 1) {
                    // We found what we are looking for.
                    done = true;
                    declarer = containingType->getStructName();
                    break;
                } else {
                    // Need to go into currFieldType.
                    containingType = SVFUtil::dyn_cast<const StructType>(currFieldType);
                    break;
                }
            } else if (currFieldOffset > fieldOffset) {
                // We've skipped over what we need.
                auto prevI = fieldOffsetI - 1;
                containingType = SVFUtil::dyn_cast<const StructType>(si->getFieldTypeWithFldIdx(*prevI));
                break;
            } else {
                // Try the next one...
            }
        }

        if (done) break;
    }

    return declarer;
}

bool VTGraph::hasNonScalarTypes(const Type *type) {
    static std::map<const Type *, bool> checkedTypes;
    if (checkedTypes.find(type) != checkedTypes.end()) {
        return checkedTypes.at(type);
    }

    if (type->isIntegerTy()) {
        checkedTypes[type] = false;
        return false;
    }

    if (type->isPointerTy()) {
        checkedTypes[type] = true;
        return true;
    }

    if (type->isFunctionTy()) {
        checkedTypes[type] = false;
        return false;
    }

    if (type->isArrayTy()) {
        bool ret = hasNonScalarTypes(type->getArrayElementType());
        checkedTypes[type] = ret;
        return ret;
    }

    if (type->isVectorTy()) {
        bool ret = hasNonScalarTypes(type->getVectorElementType());
        checkedTypes[type] = ret;
        return ret;
    }

    if (type->isStructTy()) {
        const StructType *structType = SVFUtil::dyn_cast<StructType>(type);
        if (structType->hasName()) {
            checkedTypes[type] = true;
            return true;
        }

        for (auto elementType = structType->element_begin(); elementType != structType->element_end(); ++elementType) {
            if (hasNonScalarTypes(*elementType)) {
                checkedTypes[type] = true;
                return true;
            }
        }

        checkedTypes[type] = false;
        return false;
    }

    // In case something is missed, be conservative.
    checkedTypes[type] = true;
    return true;
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

