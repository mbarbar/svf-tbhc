//===- PAGNode.h -- PAG node class-------------------------------------------//
//
//                     SVF: Static Value-Flow Analysis
//
// Copyright (C) <2013-2017>  <Yulei Sui>
//

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//===----------------------------------------------------------------------===//

/*
 * PAGNode.h
 *
 *  Created on: Nov 10, 2013
 *      Author: Yulei Sui
 */

#ifndef PAGNODE_H_
#define PAGNODE_H_

#include "MemoryModel/GenericGraph.h"

/*
 * PAG node
 */
typedef GenericNode<PAGNode,PAGEdge> GenericPAGNodeTy;
class PAGNode : public GenericPAGNodeTy {

public:
    /// Nine kinds of PAG nodes
    /// ValNode: llvm pointer value
    /// ObjNode: memory object
    /// RetNode: unique return node
    /// Vararg: unique node for vararg parameter
    /// GepValNode: tempory gep value node for field sensitivity
    /// GepValNode: tempory gep obj node for field sensitivity
    /// FIObjNode: for field insensitive analysis
    /// DummyValNode and DummyObjNode: for non-llvm-value node
    enum PNODEK {
        ValNode,
        ObjNode,
        RetNode,
        VarargNode,
        GepValNode,
        GepObjNode,
        FIObjNode,
        DummyValNode,
        DummyObjNode,
        TypeObjNode,
        IncompatibleObjNode,
        LSObjNode,
        VLEObjNode
    };


protected:
    const Value* value; ///< value of this PAG node
    PAGEdge::PAGKindToEdgeSetMapTy InEdgeKindToSetMap;
    PAGEdge::PAGKindToEdgeSetMapTy OutEdgeKindToSetMap;
    bool isTLPointer;	/// top-level pointer
    bool isATPointer;	/// address-taken pointer

public:
    /// Constructor
    PAGNode(const Value* val, NodeID i, PNODEK k);
    /// Destructor
    virtual ~PAGNode() {
    }

    ///  Get/has methods of the components
    //@{
    inline const Value* getValue() const {
        assert((this->getNodeKind() != DummyValNode && this->getNodeKind() != DummyObjNode) && "dummy node do not have value!");
        assert((SymbolTableInfo::isBlkObjOrConstantObj(this->getId())==false) && "blackhole and constant obj do not have value");
        assert(value && "value is null!!");
        return value;
    }

    /// Return type of the value
    inline virtual const Type* getType() const{
        if (value)
            return value->getType();
        return NULL;
    }

    inline bool hasValue() const {
        return (this->getNodeKind() != DummyValNode &&
                this->getNodeKind() != DummyObjNode &&
                this->getNodeKind() != TypeObjNode &&
                this->getNodeKind() != IncompatibleObjNode &&
                this->getNodeKind() != VLEObjNode &&
                this->getNodeKind() != LSObjNode &&
                (SymbolTableInfo::isBlkObjOrConstantObj(this->getId())==false) &&
                value != NULL);
    }
    /// Whether it is a pointer
    virtual inline bool isPointer() const {
        return isTopLevelPtr() || isAddressTakenPtr();
    }
    /// Whether it is a top-level pointer
    inline bool isTopLevelPtr() const {
        return isTLPointer;
    }
    /// Whether it is an address-taken pointer
    inline bool isAddressTakenPtr() const {
        return isATPointer;
    }
    /// Whether it is constant data, i.e., "0", "1.001", "str"
	inline bool isConstantData() const {
		if (hasValue())
			return SymbolTableInfo::Symbolnfo()->isConstantObjSym(value);
		else
			return false;
	}

    /// Get name of the LLVM value
    virtual const std::string getValueName() const = 0;

    /// Return the function that this PAGNode resides in. Return NULL if it is a global or constantexpr node
    virtual inline const Function* getFunction() const {
        if(value){
            if(const Instruction* inst = SVFUtil::dyn_cast<Instruction>(value))
                return inst->getParent()->getParent();
            else if (const Argument* arg = SVFUtil::dyn_cast<Argument>(value))
                return arg->getParent();
            else if (const Function* fun = SVFUtil::dyn_cast<Function>(value))
                return fun;
        }
        return NULL;
    }

    /// Get incoming PAG edges
    inline PAGEdge::PAGEdgeSetTy& getIncomingEdges(PAGEdge::PEDGEK kind) {
        return InEdgeKindToSetMap[kind];
    }

    /// Get outgoing PAG edges
    inline PAGEdge::PAGEdgeSetTy& getOutgoingEdges(PAGEdge::PEDGEK kind) {
        return OutEdgeKindToSetMap[kind];
    }

    /// Has incoming PAG edges
    inline bool hasIncomingEdges(PAGEdge::PEDGEK kind) const {
        PAGEdge::PAGKindToEdgeSetMapTy::const_iterator it = InEdgeKindToSetMap.find(kind);
        if (it != InEdgeKindToSetMap.end())
            return (!it->second.empty());
        else
            return false;
    }

    /// Has incoming VariantGepEdges
    inline bool hasIncomingVariantGepEdge() const {
        PAGEdge::PAGKindToEdgeSetMapTy::const_iterator it = InEdgeKindToSetMap.find(PAGEdge::VariantGep);
        if (it != InEdgeKindToSetMap.end()) {
            return (!it->second.empty());
        }
        return false;
    }

    /// Get incoming PAGEdge iterator
    inline PAGEdge::PAGEdgeSetTy::iterator getIncomingEdgesBegin(PAGEdge::PEDGEK kind) const {
        PAGEdge::PAGKindToEdgeSetMapTy::const_iterator it = InEdgeKindToSetMap.find(kind);
        assert(it!=InEdgeKindToSetMap.end() && "The node does not have such kind of edge");
        return it->second.begin();
    }

    /// Get incoming PAGEdge iterator
    inline PAGEdge::PAGEdgeSetTy::iterator getIncomingEdgesEnd(PAGEdge::PEDGEK kind) const {
        PAGEdge::PAGKindToEdgeSetMapTy::const_iterator it = InEdgeKindToSetMap.find(kind);
        assert(it!=InEdgeKindToSetMap.end() && "The node does not have such kind of edge");
        return it->second.end();
    }

    /// Has outgoing PAG edges
    inline bool hasOutgoingEdges(PAGEdge::PEDGEK kind) const {
        PAGEdge::PAGKindToEdgeSetMapTy::const_iterator it = OutEdgeKindToSetMap.find(kind);
        if (it != OutEdgeKindToSetMap.end())
            return (!it->second.empty());
        else
            return false;
    }

    /// Get outgoing PAGEdge iterator
    inline PAGEdge::PAGEdgeSetTy::iterator getOutgoingEdgesBegin(PAGEdge::PEDGEK kind) const {
        PAGEdge::PAGKindToEdgeSetMapTy::const_iterator it = OutEdgeKindToSetMap.find(kind);
        assert(it!=OutEdgeKindToSetMap.end() && "The node does not have such kind of edge");
        return it->second.begin();
    }

    /// Get outgoing PAGEdge iterator
    inline PAGEdge::PAGEdgeSetTy::iterator getOutgoingEdgesEnd(PAGEdge::PEDGEK kind) const {
        PAGEdge::PAGKindToEdgeSetMapTy::const_iterator it = OutEdgeKindToSetMap.find(kind);
        assert(it!=OutEdgeKindToSetMap.end() && "The node does not have such kind of edge");
        return it->second.end();
    }
    //@}

    ///  add methods of the components
    //@{
    inline void addInEdge(PAGEdge* inEdge) {
        GNodeK kind = inEdge->getEdgeKind();
        InEdgeKindToSetMap[kind].insert(inEdge);
        addIncomingEdge(inEdge);
    }

    inline void addOutEdge(PAGEdge* outEdge) {
        GNodeK kind = outEdge->getEdgeKind();
        OutEdgeKindToSetMap[kind].insert(outEdge);
        addOutgoingEdge(outEdge);
    }
    //@}
    /// Overloading operator << for dumping PAGNode value
    //@{
    friend raw_ostream& operator<< (raw_ostream &o, const PAGNode &node) {
        o << "NodeID: " << node.getId() << "\t, Node Kind: ";
        if (node.getNodeKind() == ValNode ||
                node.getNodeKind() == GepValNode ||
                node.getNodeKind() == DummyValNode) {
            o << "ValPN\n";
		} else if (node.getNodeKind() == ObjNode
				|| node.getNodeKind() == GepObjNode
				|| node.getNodeKind() == FIObjNode
				|| node.getNodeKind() == DummyObjNode
				|| node.getNodeKind() == TypeObjNode
				|| node.getNodeKind() == IncompatibleObjNode
				|| node.getNodeKind() == LSObjNode
				|| node.getNodeKind() == VLEObjNode) {
            o << "ObjPN\n";
        } else if (node.getNodeKind() == RetNode) {
            o << "RetPN\n";
        } else {
            o << "otherPN\n";
        }
        if (node.hasValue()) {
            const Value *val = node.getValue();
            if (const Function *fun = SVFUtil::dyn_cast<Function>(val))
                o << "Value: function " << fun->getName().str();
            else
                o << "Value: " << *val;
        } else {
            o << "Empty Value";
        }
        return o;
    }
    //@}
};



/*
 * Value(Pointer) node
 */
class ValPN: public PAGNode {

public:
    /// Methods for support type inquiry through isa, cast, and dyn_cast:
    //@{
    static inline bool classof(const ValPN *) {
        return true;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::ValNode ||
               node->getNodeKind() == PAGNode::GepValNode ||
               node->getNodeKind() == PAGNode::DummyValNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::ValNode ||
               node->getNodeKind() == PAGNode::GepValNode ||
               node->getNodeKind() == PAGNode::DummyValNode;
    }
    //@}

    /// Constructor
    ValPN(const Value* val, NodeID i, PNODEK ty = ValNode) :
        PAGNode(val, i, ty) {
    }
    /// Return name of a LLVM value
    inline const std::string getValueName() const {
        if (value && value->hasName())
            return value->getName();
        return "";
    }
};


/*
 * Memory Object node
 */
class ObjPN: public PAGNode {

protected:
    const MemObj* mem;	///< memory object
    /// Constructor
    ObjPN(const Value* val, NodeID i, const MemObj* m, PNODEK ty = ObjNode) :
        PAGNode(val, i, ty), mem(m) {
    }
public:
    /// Methods for support type inquiry through isa, cast, and dyn_cast:
    //@{
    static inline bool classof(const ObjPN *) {
        return true;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::ObjNode ||
               node->getNodeKind() == PAGNode::GepObjNode ||
               node->getNodeKind() == PAGNode::FIObjNode ||
               node->getNodeKind() == PAGNode::TypeObjNode ||
               node->getNodeKind() == PAGNode::IncompatibleObjNode ||
               node->getNodeKind() == PAGNode::VLEObjNode ||
               node->getNodeKind() == PAGNode::LSObjNode ||
			   node->getNodeKind() == PAGNode::DummyObjNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::ObjNode ||
               node->getNodeKind() == PAGNode::GepObjNode ||
               node->getNodeKind() == PAGNode::FIObjNode ||
               node->getNodeKind() == PAGNode::TypeObjNode ||
               node->getNodeKind() == PAGNode::IncompatibleObjNode ||
               node->getNodeKind() == PAGNode::LSObjNode ||
               node->getNodeKind() == PAGNode::DummyObjNode;
    }
    //@}

    /// Return memory object
    const MemObj* getMemObj() const {
        return mem;
    }

    /// Return name of a LLVM value
    const std::string getValueName() {
        if (value && value->hasName())
            return value->getName();
        return "";
    }
    /// Return type of the value
    inline virtual const llvm::Type* getType() const{
       return mem->getType();
    }
};


/*
 * Gep Value (Pointer) node, this node can be dynamic generated for field sensitive analysis
 * e.g. memcpy, temp gep value node needs to be created
 * Each Gep Value node is connected to base value node via gep edge
 */
class GepValPN: public ValPN {

private:
    LocationSet ls;	// LocationSet
    const Type *gepValType;
    u32_t fieldIdx;

public:
    /// Methods for support type inquiry through isa, cast, and dyn_cast:
    //@{
    static inline bool classof(const GepValPN *) {
        return true;
    }
    static inline bool classof(const ValPN * node) {
        return node->getNodeKind() == PAGNode::GepValNode;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::GepValNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::GepValNode;
    }
    //@}

    /// Constructor
    GepValPN(const Value* val, NodeID i, const LocationSet& l, const Type *ty, u32_t idx) :
        ValPN(val, i, GepValNode), ls(l), gepValType(ty), fieldIdx(idx) {
    }

    /// offset of the base value node
    inline u32_t getOffset() const {
        return ls.getOffset();
    }

    /// Return name of a LLVM value
    inline const std::string getValueName() const {
        if (value && value->hasName())
            return value->getName().str() + "_" + llvm::utostr(getOffset());
        return "offset_" + llvm::utostr(getOffset());
    }

	inline const Type* getType() const {
		return gepValType;
	}

    u32_t getFieldIdx() const {
        return fieldIdx;
    }
};


/*
 * Gep Obj node, this is dynamic generated for field sensitive analysis
 * Each gep obj node is one field of a MemObj (base)
 */
class GepObjPN: public ObjPN {
private:
    LocationSet ls;

public:
    /// Methods for support type inquiry through isa, cast, and dyn_cast:
    //@{
    static inline bool classof(const GepObjPN *) {
        return true;
    }
    static inline bool classof(const ObjPN * node) {
        return node->getNodeKind() == PAGNode::GepObjNode;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::GepObjNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::GepObjNode;
    }
    //@}

    /// Constructor
    GepObjPN(const MemObj* mem, NodeID i, const LocationSet& l) :
        ObjPN(mem->getRefVal(), i, mem, GepObjNode), ls(l) {
    }

    /// offset of the mem object
    inline const LocationSet& getLocationSet() const {
        return ls;
    }

    /// Return the type of this gep object
	inline const llvm::Type* getType() {
		return SymbolTableInfo::Symbolnfo()->getOrigSubTypeWithByteOffset(mem->getType(), ls.getByteOffset());
	}

    /// Return name of a LLVM value
    inline const std::string getValueName() const {
        if (value && value->hasName())
            return value->getName().str() + "_" + llvm::itostr(ls.getOffset());
        return "offset_" + llvm::itostr(ls.getOffset());
    }
};

/*
 * Field-insensitive Gep Obj node, this is dynamic generated for field sensitive analysis
 * Each field-insensitive gep obj node represents all fields of a MemObj (base)
 */
class FIObjPN: public ObjPN {
public:

    ///  Methods for support type inquiry through isa, cast, and dyn_cast:
    //@{
    static inline bool classof(const FIObjPN *) {
        return true;
    }
    static inline bool classof(const ObjPN * node) {
        return node->getNodeKind() == PAGNode::FIObjNode;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::FIObjNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::FIObjNode;
    }
    //@}

    /// Constructor
    FIObjPN(const Value* val, NodeID i, const MemObj* mem) :
        ObjPN(val, i, mem, FIObjNode) {
    }

    /// Return name of a LLVM value
    inline const std::string getValueName() const {
        if (value && value->hasName())
            return value->getName().str() + "_field_insensitive";
        return "field_insensitive";
    }
};

/*
 * Unique Return node of a procedure
 */
class RetPN: public PAGNode {

public:

    //@{ Methods for support type inquiry through isa, cast, and dyn_cast:
    static inline bool classof(const RetPN *) {
        return true;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::RetNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::RetNode;
    }
    //@}

    /// Constructor
    RetPN(const Function* val, NodeID i) :
        PAGNode(val, i, RetNode) {
    }

    /// Return name of a LLVM value
    const std::string getValueName() const {
        const Function* fun = SVFUtil::cast<Function>(value);
        return fun->getName().str() + "_ret";
    }
};


/*
 * Unique vararg node of a procedure
 */
class VarArgPN: public PAGNode {

public:

    //@{ Methods for support type inquiry through isa, cast, and dyn_cast:
    static inline bool classof(const VarArgPN *) {
        return true;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::VarargNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::VarargNode;
    }
    //@}

    /// Constructor
    VarArgPN(const Function* val, NodeID i) :
        PAGNode(val, i, VarargNode) {
    }

    /// Return name of a LLVM value
    inline const std::string getValueName() const {
        const Function* fun = SVFUtil::cast<Function>(value);
        return fun->getName().str() + "_vararg";
    }
};




/*
 * Dummy node
 */
class DummyValPN: public ValPN {

public:

    //@{ Methods for support type inquiry through isa, cast, and dyn_cast:
    static inline bool classof(const DummyValPN *) {
        return true;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::DummyValNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::DummyValNode;
    }
    //@}

    /// Constructor
    DummyValPN(NodeID i) : ValPN(NULL, i, DummyValNode) {
    }


    /// Return name of this node
    inline const std::string getValueName() const {
        return "dummyVal";
    }
};


/*
 * Dummy node
 */
class DummyObjPN: public ObjPN {

public:

    //@{ Methods for support type inquiry through isa, cast, and dyn_cast:
    static inline bool classof(const DummyObjPN *) {
        return true;
    }
	static inline bool classof(const PAGNode *node) {
		return node->getNodeKind() == PAGNode::DummyObjNode
				|| node->getNodeKind() == PAGNode::TypeObjNode
				|| node->getNodeKind() == PAGNode::IncompatibleObjNode
				|| node->getNodeKind() == PAGNode::LSObjNode;
	}
	static inline bool classof(const GenericPAGNodeTy *node) {
		return node->getNodeKind() == PAGNode::DummyObjNode
				|| node->getNodeKind() == PAGNode::TypeObjNode
				|| node->getNodeKind() == PAGNode::IncompatibleObjNode
				|| node->getNodeKind() == PAGNode::LSObjNode;
	}
    //@}

    /// Constructor
    DummyObjPN(NodeID i,const MemObj* m, PNODEK ty = DummyObjNode) : ObjPN(NULL, i, m, ty) {
    }

    /// Return name of this node
    inline const std::string getValueName() const {
        return "dummyObj";
    }
};


/*
 * Dummy node
 */
class TypeObjPN: public DummyObjPN {

private:
	const Type* type;
public:

    //@{ Methods for support type inquiry through isa, cast, and dyn_cast:
    static inline bool classof(const TypeObjPN *) {
        return true;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::TypeObjNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::TypeObjNode;
    }
    //@}

    /// Constructor
    TypeObjPN(NodeID i, const MemObj* m, const Type* t) : DummyObjPN(i, m, TypeObjNode), type(t) {
    }

    inline virtual const Type* getType() const{
        return type;
    }

    /// Return name of this node
    inline const std::string getValueName() const {
        std::string str;
        raw_string_ostream rawstr(str);
        type->print(rawstr);
        return "TypeObj:" + rawstr.str();
    }
};

/*
 * Incompatible node for ITC. Holds many real object nodes of incompatible types.
 */
class IncompatibleObjPN: public DummyObjPN {
private:
    std::set<const ObjPN *> objectNodes;

public:
    //@{ Methods for support type inquiry through isa, cast, and dyn_cast:
    static inline bool classof(const IncompatibleObjPN *) {
        return true;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::IncompatibleObjNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::IncompatibleObjNode;
    }
    //@}

    /// Constructor
    IncompatibleObjPN(NodeID i, const MemObj* m)
        : DummyObjPN(i, m, IncompatibleObjNode) {
    }

    void addObjectNode(const ObjPN *objNode) {
        objectNodes.insert(objNode);
    }

    std::set<const ObjPN *> getObjectNodes(void) const {
        return objectNodes;
    }

    /// Returns the number of object nodes this node holds.
    size_t objectNodeCount(void) {
        return objectNodes.size();
    }

    bool hasObjects(void) const {
        return !objectNodes.empty();
    }

    /// Return name of this node
    inline const std::string getValueName() const {
        return "IncompatibleObjPN: representing" + std::to_string(objectNodes.size()) + " nodes\n";
    }
};

/*
 * Vaguely Location Equivalent nodes for AndersenVLE. Holds many object nodes.
 */
class VLEObjPN: public DummyObjPN {
private:
    /// VLE object nodes this node represents.
    std::set<const ObjPN *> objects;

public:
    //@{ Methods for support type inquiry through isa, cast, and dyn_cast:
    static inline bool classof(const VLEObjPN *) {
        return true;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::VLEObjNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::VLEObjNode;
    }
    //@}

    /// Constructor
    VLEObjPN(NodeID i, const MemObj* m)
        : DummyObjPN(i, m, VLEObjNode) {
    }

    void addObjectNode(const ObjPN *objNode) {
        objects.insert(objNode);
    }

    std::set<const ObjPN *> getObjects(void) const {
        return objects;
    }

    /// Returns the number of object nodes this node holds.
    size_t holds(void) const {
        return objects.size();
    }

    bool hasNodes(void) const {
        return !objects.empty();
    }

    /// Fills underlyingObjs with all underlying objects.
    void getUnderlyingObjects(std::set<const ObjPN *> &underlyingObjs) const {
        for (std::set<const ObjPN *>::iterator objI = objects.begin(); objI != objects.end(); ++objI) {
            if (const VLEObjPN *vleObj = SVFUtil::dyn_cast<VLEObjPN>(*objI)) vleObj->getUnderlyingObjects(underlyingObjs);
            else underlyingObjs.insert(*objI);
        }
    }

    /// Returns all underlying objects.
    std::set<const ObjPN *> getUnderlyingObjects(void) const {
        std::set<const ObjPN *> underlyingObjs;
        getUnderlyingObjects(underlyingObjs);
        // TODO: caching is pretty easy and possible optimisation.
        return underlyingObjs;
    }

    /// Return name of this node
    inline const std::string getValueName() const {
        return "VLEObjPN: representing " + std::to_string(objects.size()) + " nodes\n";
    }
};

/*
 * Load Store (TODO...) nodes for AndersenLS. Appears as one object but actually represents its PTS.
 */
class LSObjPN: public DummyObjPN {
private:
    NodeID originalId;

    /// Nodes whose points to sets this node actually represents.
    std::set<NodeID> ptsNodes;
    std::set<NodeID> actualPts;

    std::set<NodeID> naturalPtsNodes;
    std::set<NodeID> unnaturalPtsNodes;

    std::map<NodeID, bool> isNatural;
    bool allNatural;

public:
    //@{ Methods for support type inquiry through isa, cast, and dyn_cast:
    static inline bool classof(const LSObjPN *) {
        return true;
    }
    static inline bool classof(const PAGNode *node) {
        return node->getNodeKind() == PAGNode::LSObjNode;
    }
    static inline bool classof(const GenericPAGNodeTy *node) {
        return node->getNodeKind() == PAGNode::LSObjNode;
    }
    //@}

    /// Constructor
    LSObjPN(NodeID i, const MemObj* m, NodeID originalId)
        : DummyObjPN(i, m, LSObjNode) {
        this->originalId = originalId;
    }

    void addPtsNode(NodeID nodeId, bool natural) {
        ptsNodes.insert(nodeId);

        if (natural) naturalPtsNodes.insert(nodeId);
        else unnaturalPtsNodes.insert(nodeId);

        isNatural[nodeId] = natural;
    }

    void removePtsNode(NodeID nodeId) {
        ptsNodes.erase(nodeId);
        isNatural.erase(nodeId);
    }

    std::set<NodeID> &getPtsNodes(void) {
        return ptsNodes;
    }

    std::set<NodeID> &getNaturalPtsNodes(void) {
        return naturalPtsNodes;
    }

    std::set<NodeID> &getUnnaturalPtsNodes(void) {
        return unnaturalPtsNodes;
    }

    void setNaturalFlow(const NodeID nodeId, bool natural) {
        isNatural[nodeId] = natural;
    }

    bool flowedInNaturally(NodeID nodeId) {
        return allNatural || isNatural[nodeId];
    }

    /// Returns the number of object nodes this node represents.
    size_t represents(void) {
        return ptsNodes.size();
    }

    std::set<NodeID> &getActualPts() {
        return actualPts;
    }

    void setActualPts(std::set<NodeID> actualPts) {
        this->actualPts.clear();
        this->actualPts.insert(actualPts.begin(), actualPts.end());
    }

    /// Return name of this node
    inline const std::string getValueName() const {
        return "LSObjPN: representing " + std::to_string(ptsNodes.size()) + " points to sets\n";
    }
};

#endif /* PAGNODE_H_ */
