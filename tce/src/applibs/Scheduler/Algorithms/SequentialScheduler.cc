/*
    Copyright 2002-2008 Tampere University of Technology.  All Rights
    Reserved.

    This file is part of TTA-Based Codesign Environment (TCE).

    TCE is free software; you can redistribute it and/or modify it under the
    terms of the GNU General Public License version 2 as published by the Free
    Software Foundation.

    TCE is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
    details.

    You should have received a copy of the GNU General Public License along
    with TCE; if not, write to the Free Software Foundation, Inc., 51 Franklin
    St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/
/**
 * @file SequentialScheduler.cc
 *
 * Definition of SequentialScheduler class.
 *
 * @author Heikki Kultala 2008 (hkultala-no.spam-cs.tut.fi)
 * @note rating: red
 */

// using CFG causes some 9% slowdown.
// even if this is not defined schedling is still done one BB at time.
//#define USE_CFG

#include <string>

#include <boost/config.hpp>
#include <boost/format.hpp>

#ifdef USE_CFG
#include "ControlFlowGraph.hh"
#endif

#include "AssocTools.hh"
#include "SimpleResourceManager.hh"
#include "Procedure.hh"
#include "ProgramOperation.hh"
#include "ControlUnit.hh"
#include "Machine.hh"
#include "UniversalMachine.hh"
#include "Instruction.hh"
#include "InstructionReference.hh"
#include "BasicBlock.hh"
#include "ControlFlowGraphPass.hh"
#include "RegisterCopyAdder.hh"
#include "SchedulerPass.hh"
#include "Guard.hh"
#include "MoveGuard.hh"

#include "SequentialScheduler.hh"

//#define DEBUG_OUTPUT
//#define DEBUG_REG_COPY_ADDER
//#define CFG_SNAPSHOTS


class SequentialSelector;

/**
 * Constructs the sequential scheduler.
 *
 * @param data Interpass data
 */
SequentialScheduler::SequentialScheduler(
    InterPassData& data) :
    BasicBlockPass(data), 
    ControlFlowGraphPass(data),
    ProcedurePass(data),
    ProgramPass(data), 
     rm_(NULL) {
}

/**
 * Destructor.
 */
SequentialScheduler::~SequentialScheduler() {
}

/**
 * Schedules a single basic block.
 *
 * @param bb The basic block to schedule.
 * @param targetMachine The target machine.
 * @exception Exception several TCE exceptions can be thrown in case of
 *            a scheduling error.
 */
void
SequentialScheduler::handleBasicBlock(
    BasicBlock& bb, 
    const TTAMachine::Machine& targetMachine)
    throw (Exception) {

    if (bb.instructionCount() == 0)
        return;

    targetMachine_ = &targetMachine;
    rm_ = new SimpleResourceManager(targetMachine);

    int cycle = 0;

    SequentialMoveNodeSelector selector(bb);

    // loop as long as selector gives things to schedule
    MoveNodeGroup moves = selector.candidates();
    while (moves.nodeCount() > 0) {

        MoveNode& firstMove = moves.node(0);
        if (firstMove.isOperationMove()) {
            cycle = scheduleOperation(moves,cycle) + 1;
        } else {
            cycle = scheduleMove(cycle, firstMove) +1;
        }

        if (!moves.isScheduled()) {
            std::string message = " Move(s) did not get scheduled: ";
            for (int i = 0; i < moves.nodeCount(); i++) {
                message += moves.node(i).toString() + " ";
            }
            throw ModuleRunTimeError(__FILE__, __LINE__, __func__, message);
        }

        for (int moveIndex = 0; moveIndex < moves.nodeCount(); ++moveIndex) {
            MoveNode& moveNode = moves.node(moveIndex);
            selector.notifyScheduled(moveNode);
        }
        moves = selector.candidates();
    }

    copyRMToBB(*rm_, bb, targetMachine);

    delete rm_; rm_ = NULL; 
}

#ifdef DEBUG_REG_COPY_ADDER
static int graphCount = 0;
#endif

/**
 * Schedules moves in a single operation execution.
 *
 * Assumes the given MoveNodeGroup contains all moves in the operation
 * execution. Also assumes that all inputs to the MoveNodeGroup have
 * been scheduled.
 *
 * @param moves Moves of the operation execution.
 * @return returns the last cycle of the operation.
 */
int
SequentialScheduler::scheduleOperation(MoveNodeGroup& moves, int earliestCycle)
    throw (Exception) {

    ProgramOperation& po =
        (moves.node(0).isSourceOperation())?
        (moves.node(0).sourceOperation()):
        (moves.node(0).destinationOperation());

    RegisterCopyAdder regCopyAdder(BasicBlockPass::interPassData(), *rm_);


    // TODO: registercopyader and ddg..
    RegisterCopyAdder::AddedRegisterCopies addedCopies =
        regCopyAdder.addMinimumRegisterCopies(po, *targetMachine_, NULL);

#ifdef DEBUG_REG_COPY_ADDER
    const int tempsAdded = addedCopies.count_;
#endif

    MoveNodeSet tempSet;
    for (int i = 0; i < moves.nodeCount(); i++) {
        // MoveNodeGroup relates to DDG so we copy it to more
        // simple MoveNodeSet container
        tempSet.addMoveNode(moves.node(i));
    }

    int triggerCycle  = scheduleOperandWrites(
        earliestCycle, moves, addedCopies);
    if (triggerCycle == -1) {
        throw ModuleRunTimeError(
            __FILE__,__LINE__,__func__,
            "Scheduling operands failed for: " +moves.toString());
    }
    
    int lastCycle = scheduleResultReads(triggerCycle+1, moves, addedCopies);

    if (lastCycle == -1) {
        throw ModuleRunTimeError(
            __FILE__,__LINE__,__func__,
            "Scheduling results failed for: " +moves.toString());
    }
    return lastCycle;
}

/**
 * Schedules operand moves of an operation execution.
 *
 * Assumes the given MoveNodeGroup contains all moves in the operation
 * execution. Also assumes that all inputs to the MoveNodeGroup have
 * been scheduled. 
 * Exception to this are the possible temporary register
 * copies inserted before the operand move due to missing connectivity.
 * If found, the temp moves are scheduled atomically with the operand move.
 * Assumes top-down scheduling.
 *
 * @param cycle Earliest cycle for starting scheduling of operands
 * @param moves Moves of the operation execution.
 * @return The cycle the trigger got scheduled
 */
int
SequentialScheduler::scheduleOperandWrites(
    int cycle, MoveNodeGroup& moves,
    RegisterCopyAdder::AddedRegisterCopies& regCopies)
    throw (Exception) {

    // Counts operands that are not scheduled at beginning.
    int scheduledMoves = 0;
    MoveNode* trigger = NULL;
    MoveNode& firstNode = moves.node(0);
    ProgramOperation& po = firstNode.destinationOperation();
    

    for (int i = 0; i < moves.nodeCount(); i++) {

        MoveNode& node = moves.node(i);
        // result read?
        if (!node.isDestinationOperation()) {
            continue;
        }

        cycle = scheduleInputOperandTempMoves(cycle, node, regCopies);
        scheduleMove(cycle, node);
        scheduledMoves++;
        
        TTAProgram::Terminal& dest = node.move().destination();
        // got triger?
        if (dest.isFUPort() && dest.isTriggering()) {
            
            // if all operands not scheduled, delay trigger
            if (scheduledMoves < po.inputMoveCount()) {
                trigger = &node;
                unschedule(node);
                scheduledMoves--;
                continue;
            }
        }
        cycle = node.cycle() +1;
    }
    // trigger scheduling delayed, schedule at end
    if (trigger != NULL && !trigger->isScheduled()) {
        assert(scheduledMoves == po.inputMoveCount()-1);
        return scheduleMove(cycle, *trigger);
    }
    return cycle-1;
}

/**
 * Schedules the result read moves of an operation execution.
 *
 * Assumes the given MoveNodeGroup contains all moves in the operation
 * execution. Also assumes that all operand moves have been scheduled.
 *
 * @param moves Moves of the operation execution.
 * @return cycle of last operand read, or -1 if fails
 */
int
SequentialScheduler::scheduleResultReads(
    int cycle, MoveNodeGroup& moves, 
    RegisterCopyAdder::AddedRegisterCopies& regCopies)
    throw (Exception) {
    
    for (int moveIndex = 0; moveIndex < moves.nodeCount(); ++moveIndex) {
        MoveNode& node = moves.node(moveIndex);
        
        if (!node.isScheduled()) {
            if (!node.isSourceOperation()) {
                throw InvalidData(
                    __FILE__, __LINE__, __func__,
                    (boost::format("Move to schedule '%s' is not "
                    "result move!") % node.toString()).str());
            }

            cycle = std::max(cycle, node.earliestResultReadCycle());
            cycle = scheduleMove(cycle, node) +1;
            cycle = scheduleResultTempMoves(cycle, node, regCopies);

            if (!node.isScheduled()) {
                throw InvalidData(
                    __FILE__, __LINE__, __func__,
                    (boost::format("Move '%s' did not get scheduled!") 
                    % node.toString()).str());                
            }
        }
    }
    return cycle-1;
}

/**
 * Schedules a single move to the earliest possible cycle, taking in
 * account the DDG, resource constraints, and latencies in producing
 * source values.
 *
 * This method assumes the move is possible to schedule with regards to
 * connectivity and resources. Short immediates are converted to long
 * immediates when needed.
 *
 * @param move The move to schedule.
 * @param earliestCycle The earliest cycle to try.
 * @return cycle where the move got scheduled.
 */
int
SequentialScheduler::scheduleMove(
    int earliestCycle,
    MoveNode& moveNode)
    throw (Exception) {

    if (moveNode.isScheduled()) {
        throw InvalidData(
            __FILE__, __LINE__, __func__,
            (boost::format("Move '%s' is already scheduled!") 
            % moveNode.toString()).str());                
    }            

    // if it's a conditional move then we have to be sure that the guard
    // is defined before executing the move
    if (!moveNode.move().isUnconditional()) {
        int guardLatency =
            targetMachine_->controlUnit()->globalGuardLatency();

        earliestCycle += guardLatency; // jut for sure?
        TTAMachine::Guard& guard = moveNode.move().guard().guard();
        TTAMachine::RegisterGuard* rg = 
            dynamic_cast<TTAMachine::RegisterGuard*>(&guard);
        if (rg != NULL) {
            guardLatency += rg->registerFile()->guardLatency();
        }
        earliestCycle+= guardLatency;
    }

    // RM hasConnection takes MoveNodeSet, however is called only for one
    // moveNode here.
    MoveNodeSet tempSet;
    tempSet.addMoveNode(moveNode);
    if (moveNode.isSourceConstant() && 
        !moveNode.move().hasAnnotations(
            TTAProgram::ProgramAnnotation::ANN_REQUIRES_LIMM)) {
        // If source is constant and node does not have annotation already,
        // we add it if constant can not be transported so IU broker and 
        // OutputPSocket brokers will add Immediate
        // Example : 999999 -> integer0.2
        if (!rm_->canTransportImmediate(moveNode)){
            TTAProgram::ProgramAnnotation annotation(
                TTAProgram::ProgramAnnotation::ANN_REQUIRES_LIMM);
            moveNode.move().setAnnotation(annotation); 
            
        } else if (!moveNode.isDestinationOperation() &&
                   rm_->earliestCycle(rm_->largestCycle()+1,moveNode) == -1) {
            // If source is constant and node does not have annotation 
            // already, we add it if node has no connection, so IU broker and 
            // OutputPSocket brokers will add Immediate
            // Example: 27 -> integer0.2
            // With bus capable of transporting 27 as short immediate but
            // no connection from that bus to integer0 unit
            TTAProgram::ProgramAnnotation annotation(
                TTAProgram::ProgramAnnotation::ANN_REQUIRES_LIMM);
            moveNode.move().setAnnotation(annotation);    
        }
    } 
    // annotate the return move otherwise it might get undetected in the
    // simulator after the short to long immediate conversion and thus
    // stopping simulation automatically might not work
    if (moveNode.isSourceConstant() &&
        moveNode.move().isReturn() && 
        !rm_->canTransportImmediate(moveNode)) {
        TTAProgram::ProgramAnnotation annotation(
            TTAProgram::ProgramAnnotation::ANN_STACKFRAME_PROCEDURE_RETURN);
        moveNode.move().setAnnotation(annotation);
    }
    
    earliestCycle = rm_->earliestCycle(earliestCycle, moveNode);
    if (earliestCycle == -1 || earliestCycle == INT_MAX) {
        if (moveNode.isSourceConstant() &&
            !moveNode.isDestinationOperation() &&
            moveNode.move().hasAnnotations(
                TTAProgram::ProgramAnnotation::ANN_REQUIRES_LIMM)) {
            // If earliest cycle returns -1 and source is constant 
            // and moveNode needs long immediate 
            // there is most likely missing long immediate unit            
            std::string msg = "Assignment of MoveNode " + moveNode.toString();
            msg += " failed! Most likely missing Long Immediate Unit";
            msg += " or Instruction Template!";
            throw IllegalMachine(
                __FILE__, __LINE__, __func__, msg);            
        }
        std::string msg = "Assignment of MoveNode " + moveNode.toString();
        msg += " failed!";
            throw ModuleRunTimeError(
                __FILE__, __LINE__, __func__, msg);
    }
    rm_->assign(earliestCycle,  moveNode);
    if (!moveNode.isScheduled()) {
        throw ModuleRunTimeError(
            __FILE__, __LINE__, __func__, 
            (boost::format("Assignment of MoveNode '%s' failed!") 
            % moveNode.toString()).str());
    }
    return earliestCycle;
}

/**
 * Schedules the (possible) temporary register copy moves (due to missing
 * connectivity) preceeding the given input move.
 *
 * @param operandMove The move of which temp moves to schedule.
 * @return cycle next available cycle
 *
 */
int
SequentialScheduler::scheduleInputOperandTempMoves(
    int cycle, MoveNode& operandMove, RegisterCopyAdder::AddedRegisterCopies& regCopies)
    throw (Exception) {
    
    if (regCopies.count_ > 0) {
        if (AssocTools::containsKey(regCopies.copies_,&operandMove)) {
            RegisterCopyAdder::MoveNodePair& mnp = 
                regCopies.copies_[&operandMove];
            if (mnp.second != NULL) {
                cycle = scheduleMove(cycle, *mnp.second) +1;
            }
            if (mnp.first != NULL) {
                cycle = scheduleMove(cycle, *mnp.first) +1;
            }
        }
    }
    return cycle;
}

/**
 * Schedules the (possible) temporary register copy moves (due to missing
 * connectivity) succeeding the given result move.
 *
 * @param operandMove The move of which temp moves to schedule.
 * @return cycle next available cycle
 *
 */
int
SequentialScheduler::scheduleResultTempMoves(
    int cycle, MoveNode& resultMove, 
    RegisterCopyAdder::AddedRegisterCopies& regCopies)
    throw (Exception) {
    
    if (regCopies.count_ > 0) {
        if (AssocTools::containsKey(regCopies.copies_,&resultMove)) {
            RegisterCopyAdder::MoveNodePair& mnp = 
                regCopies.copies_[&resultMove];
            assert(mnp.second == NULL && "no two temp moves for result");
            if (mnp.first != NULL) {
                cycle = scheduleMove(cycle, *mnp.first) +1;
            }
        }
    }
    return cycle;
}



/**
 * Unschedules the given move.
 *
 * Also restores a possible short immediate source in case it was converted
 * to a long immediate register read during scheduling.
 *
 * @param moveNode Move to unschedule.
 */
void
SequentialScheduler::unschedule(MoveNode& moveNode) {    
    if (!moveNode.isScheduled()) {
        throw InvalidData(
            __FILE__, __LINE__, __func__,
            (boost::format("Trying to unschedule move '%s' which "
            "is not scheduled!") % moveNode.toString()).str());
    }
    rm_->unassign(moveNode);
    if (moveNode.move().hasAnnotations(
        TTAProgram::ProgramAnnotation::ANN_REQUIRES_LIMM)) {
        // If we added annotation during scheduleMove delete it
        moveNode.move().removeAnnotations(
        TTAProgram::ProgramAnnotation::ANN_REQUIRES_LIMM);
    }    
    if (moveNode.isScheduled() || moveNode.isPlaced()) {
        throw InvalidData(
            __FILE__, __LINE__, __func__,
            (boost::format("Unscheduling of move '%s' failed!") 
            % moveNode.toString()).str());
    }
}

/**
 * Schedules a procedure.
 *
 * The original procedure is modified during scheduling.
 *
 * @param procedure The procedure to schedule.
 * @param targetMachine The target machine.
 * @exception Exception In case of an error during scheduling. The exception
 *            type can be any subtype of Exception.
 */
void
SequentialScheduler::handleProcedure(
    TTAProgram::Procedure& procedure,
    const TTAMachine::Machine& targetMachine)
    throw (Exception) {

#ifdef USE_CFG
    ControlFlowGraph cfg(procedure);

#ifdef CFG_SNAPSHOTS
    cfg.writeToDotFile(procedure.name() + "_cfg.dot");    
#endif

    handleControlFlowGraph(cfg, targetMachine);

    // now all basic blocks are scheduled, let's put them back to the
    // original procedure
    copyCfgToProcedure(procedure, cfg);

#else
    std::vector<BasicBlock*> basicBlocks;
    std::vector<int> bbAddresses;
    createBasicBlocks(procedure, basicBlocks, bbAddresses);

    for (unsigned int i = 0; i < basicBlocks.size();i++) {
        handleBasicBlock(*basicBlocks[i], targetMachine);
    }

    copyBasicBlocksToProcedure(procedure, basicBlocks, bbAddresses);
#endif

}

/**
 * Schedules all nodes in a control flow graph.
 *
 * The original control flow graph nodes are modified during scheduling.
 *
 * @param cfg The control flow graph to schedule.
 * @param targetMachine The target machine.
 * @exception Exception In case of an error during scheduling. The exception
 *            type can be any subtype of Exception.
 */
void
SequentialScheduler::handleControlFlowGraph(
    ControlFlowGraph& cfg,
    const TTAMachine::Machine& targetMachine)
    throw (Exception) {

    ControlFlowGraphPass::executeBasicBlockPass(cfg, targetMachine, *this);
}

/**
 * Schedules a program.
 *
 * The original program is modified during scheduling.
 *
 * @param program The program to schedule.
 * @param targetMachine The target machine.
 * @exception Exception In case of an error during scheduling. The exception
 *            type can be any subtype of Exception.
 */
void
SequentialScheduler::handleProgram(
    TTAProgram::Program& program,
    const TTAMachine::Machine& targetMachine)
    throw (Exception) {

    ProgramPass::executeProcedurePass(program, targetMachine, *this);
}

/**
 * A short description of the pass, usually the optimization name,
 * such as "basic block scheduler".
 *
 * @return The description as a string.
 */
std::string
SequentialScheduler::shortDescription() const {
    return "Sequential Instruction scheduler";
}

/**
 * Optional longer description of the pass.
 *
 * This description can include usage instructions, details of choice of
 * algorithmic details, etc.
 *
 * @return The description as a string.
 */
std::string
SequentialScheduler::longDescription() const {
    return "Sequential Instruction scheduler";
}

/**
 * Splits a procedure into basic blocks.
 */
void 
SequentialScheduler::createBasicBlocks(
    TTAProgram::Procedure& proc, 
    std::vector<BasicBlock*> &basicBlocks,
    std::vector<int>& bbAddresses) {
    TTAProgram::InstructionReferenceManager& irm = 
        proc.parent().instructionReferenceManager();
    BasicBlock* currentBB = NULL;
    int lastStartAddress = 0;
    // loop thru all instructions in the given BB.
    for (int i = 0; i < proc.instructionCount(); i++) {
        TTAProgram::Instruction& ins = proc.instructionAtIndex(i);
        TTAProgram::Instruction* insCopy = ins.copy();

        // if has references, starts a new BB, from this instruction.
        if (irm.hasReference(ins)) {
            if (currentBB != NULL) {
                // only add non-empty BBs.
                if (currentBB->instructionCount() != 0) {
                    basicBlocks.push_back(currentBB);
                    bbAddresses.push_back(lastStartAddress);
                } else {
                    delete currentBB;
                }
            }
            currentBB = new BasicBlock;
            lastStartAddress = ins.address().location();
            // update instruction references.
//            irm.replace(ins, *insCopy);
        }
        assert(currentBB != NULL); // first ins of proc should have a ref.
        currentBB->add(insCopy);        

        // jump or call starts a new BB, after this instruction.
        if (ins.hasControlFlowMove()) {
            basicBlocks.push_back(currentBB);
            bbAddresses.push_back(lastStartAddress);
            currentBB = new BasicBlock;
            lastStartAddress = ins.address().location() + 1;
        }
    }
    
    // at end, add last BB if non-empty
    if (currentBB->instructionCount() != 0) {
        basicBlocks.push_back(currentBB);
        bbAddresses.push_back(lastStartAddress);
    } else {
        delete currentBB;
    }
}

void 
SequentialScheduler::copyBasicBlocksToProcedure(
    TTAProgram::Procedure& proc, 
    std::vector<BasicBlock*>& basicBlocks,
    std::vector<int>& bbAddresses) {
    TTAProgram::InstructionReferenceManager& irm = 
        proc.parent().instructionReferenceManager();

    for (unsigned int i = 0; i < basicBlocks.size(); i++) {
        BasicBlock& bb = *basicBlocks.at(i);
        TTAProgram::Instruction& bbIns = bb.instructionAtIndex(0);
        TTAProgram::Instruction& oldProcIns = proc.instructionAt(
            bbAddresses[i]);
        if (irm.hasReference(oldProcIns)) {
            irm.replace(oldProcIns, bbIns);
        }
    }

    proc.clear();
    for (unsigned int i = 0; i < basicBlocks.size(); i++) {
        BasicBlock& bb = *basicBlocks.at(i);

        // first one is a special case. can contain ref which need to 
        // be update
        TTAProgram::Instruction& ins = bb.firstInstruction();
        TTAProgram::Instruction* insCopy = ins.copy();
        proc.CodeSnippet::add(insCopy); // delay address fix

        if (irm.hasReference(ins)) {
            irm.replace(ins, *insCopy);
        }
        
        for (int j = 1; j < bb.instructionCount(); j++) {
            TTAProgram::Instruction& ins = bb.instructionAtIndex(j);
            TTAProgram::Instruction* insCopy = ins.copy();
            proc.CodeSnippet::add(insCopy); // delay address fix
        }
    }
 
    // update inst addresses
    if (proc.isInProgram()) {
        if (!(&proc == &proc.parent().lastProcedure())) {
            proc.parent().moveProcedure(
                proc.parent().nextProcedure(proc), 
                proc.instructionCount());
        }
    }
}
