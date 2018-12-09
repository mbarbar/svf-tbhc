#include "WPA/VTAnalysis.h"

using namespace SVFUtil;

void VTAnalysis::validateTests() {
    for (u32_t i = 0; i < svfMod.getModuleNum(); ++i) {
        Module *module = svfMod.getModule(i);
        if (Function* checkFun = module->getFunction("_Z9checkTypePvS_")) {
            if (!checkFun->use_empty())
                SVFUtil::outs() << "[" << this->PTAName() << "] Checking" << "\n";

            for (Value::user_iterator i = checkFun->user_begin(), e = checkFun->user_end(); i != e; ++i)
                if (SVFUtil::isa < CallInst > (*i) || SVFUtil::isa < InvokeInst > (*i)) {
                    CallSite cs(*i);
                    assert(cs.getNumArgOperands() == 2 && "arguments (pointer, string)!!");

                    Value* v1 = cs.getArgOperand(0);
                    Value* v2 = cs.getArgOperand(1);

                    Instruction* inst = SVFUtil::cast<Instruction>(v2);
                    const Type* expectedType = inst->getType();
                    if (const Instruction* preInst =  inst->getPrevNode()){
                        if (const CastInst* cast = SVFUtil::dyn_cast<CastInst>(preInst)){
                            expectedType = SVFUtil::dyn_cast<PointerType>(cast->getType());
                        }
                    }

                    if (expectedType->isPointerTy()) expectedType = SVFUtil::dyn_cast<StructType>(expectedType->getPointerElementType());

                    NodeID node1 = pag->getValueNode(v1);
                    PointsTo& pts = this->getPts(node1);
                    for(PointsTo::iterator it = pts.begin(), eit = pts.end(); it!=eit; ++it){
                        ObjPN* obj = SVFUtil::cast<ObjPN>(pag->getPAGNode(*it));
                        const Type *actualType = obj->getType();
                        if (actualType == expectedType)
                            outs() << sucMsg("\t SUCCESS:") << " check <id:" << obj->getId() << ", type:" << expectedType << "> at ("
                                   << getSourceLoc(inst) << ")\n";
                        else
                            errs() << errMsg("\t FAIL :") << " check <id:" << obj->getId() << ", type:" << expectedType << "> at ("
                                   << getSourceLoc(inst) << ")\n";
                    }
				}
		}
	}

}


VTGraph* VTAnalysis::createVTGraph(SVFModule svfModule) {
    /// Build Constraint Graph
    VTGraph *vtg = new VTGraph(pag, svfModule);
    consCG = vtg;
    vtg->dump("vtg_initial");
    vtg->removeMemoryObjectNodes();
    if (!vtaPlus) vtg->collapseFields();

    VSCC* vscc = new VSCC(vtg);
    vscc->find();

    NodeBS changedRepNodes;
    NodeStack & topoOrder = vscc->topoNodeStack();
    while (!topoOrder.empty()) {
        NodeID repNodeId = topoOrder.top();
        topoOrder.pop();
        // merge sub nodes to rep node
        const NodeBS& subNodes = vscc->subNodes(repNodeId);
        mergeSccNodes(repNodeId,subNodes,changedRepNodes);
    }

    // update rep/sub relation in the constraint graph.
    // each node will have a rep node
    for(NodeBS::iterator it = changedRepNodes.begin(), eit = changedRepNodes.end(); it!=eit; ++it)
        updateNodeRepAndSubs(*it);

    return vtg;
}

