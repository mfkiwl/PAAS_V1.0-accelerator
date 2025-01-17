#include "arch/locked_mem.hh"
#include "arch/mmapped_ipr.hh"
#include "arch/utility.hh"
#include "base/bigint.hh"
#include "config/the_isa.hh"

#include "cpu/fpgacpu/fpgacpu.hh"
#include "cpu/exetrace.hh"
#include "debug/Config.hh"
#include "debug/Drain.hh"
#include "debug/ExecFaulting.hh"
#include "debug/SimpleCPU.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "params/FpgaCPU.hh"
#include "sim/faults.hh"
#include "sim/full_system.hh"
#include "sim/system.hh"
//#include "arch/x86/process.hh"
#include <iostream>
#include "debug/Mwait.hh"
#include<unistd.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<limits.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<unistd.h>

#include<unistd.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<fcntl.h>
#include<limits.h>
#include<sys/types.h>
#include<sys/stat.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include "sim/sim_exit.hh"

using namespace std;
using namespace TheISA;

// following part is for communication between two processes
// ------------------------------------------------------------------------------------

void FpgaCPU::createShare()
{
	running = 1;
	shared = NULL;
	FILE *t0 = fopen("fpga_share.txt","w");
	std::hash<std::string> h;
	size_t sharedid = h(name())%100000+getpid();
    shmid = shmget((key_t)sharedid, sizeof(struct shared_use_st), 0666|IPC_CREAT);
	fprintf(t0,"%d\n",(int)sharedid);
	fclose(t0);
    if(shmid == -1)
    {
        fprintf(stderr, "shmget failed\n");
        exit(EXIT_FAILURE);
    }
    shm = shmat(shmid, (void*)0, 0);
    if(shm == (void*)-1)
    {
        fprintf(stderr, "shmat failed\n");
        exit(EXIT_FAILURE);
    }
    printf("Memory attached at %llX\n", (unsigned long long)shm);
}

void FpgaCPU::deleteShare()
{
    if(shmdt(shm) == -1)
    {
        fprintf(stderr, "shmdt failed\n");
        exit(EXIT_FAILURE);
    }
}

//----------------------------------------------------------------------------------------
//The above part is for the communication between two processes


void
FpgaCPU::init()
{
    BaseSimpleCPU::init();
	if (controlPort.isConnected())
		controlPort.sendRangeChange();
	else
		fatal("FPGA controlPort is unconnected!\n");
}



void
FpgaCPU::FpgaCPUPort::TickEvent::schedule(PacketPtr _pkt, Tick t)
{
   // cout << "FpgaCPU::FpgaCPUPort::TickEvent::schedule(PacketPtr _pkt, Tick t)"  << endl;
    pkt = _pkt;
    cpu->schedule(this, t);
}


void
FpgaCPU::FpgaCPUPort::TickEvent::schedule(WholeTranslationState *_state, Tick t)
{
   // cout << "FpgaCPU::FpgaCPUPort::TickEvent::schedule(PacketPtr _pkt, Tick t)"  << endl;
    state = _state;
    cpu->schedule(this, t);
}


//make sure that the fetch action will be executed before any other event
//by setting the priority of fetchEvent higher than other default event (eventq.hh)
FpgaCPU::FpgaCPU(FpgaCPUParams *p)
    : BaseSimpleCPU(p), noL1(p->noL1), latency(p->reg_lat),num_output_fpga(p->num_output_fpga), num_input_fpga(p->num_input_fpga), ControlAddr(p->fpga_bus_addr), fetchTranslation(this), icachePort(this),
      dcachePort(this), controlPort(this,p),dmaPort(this,p->system), ifetch_pkt(NULL), dcache_pkt(NULL),
      previousCycle(0),fetchEvent(this,false,-51), releaseEvent(this), dequeueEvent(this),baseaddress_control_fpga(p->baseaddress_control_fpga),size_control_fpga(p->size_control_fpga),
	  moduleName(p->ModuleName),show_address(p->show_address),dma_available(p->dma_available),dma_size(p->dma_size),ACP(p->ACP),Reconfigurable(p->Reconfigurable),
	  Reconfiguration_time(p->Reconfiguration_time),reconfigurationEvent(this),Protocol_shakehand(p->Protocol_shakehand)
	  
{
    _status = Idle;
	OccupyFPGA = 0;
	curThread = 0;
	dma_read_done=0;
	dma_write_done=0;
	dma_write_begin=0;
	dma_read_begin=0;
	already_reset=0;
	configured=0;
	if (dma_available)
	{	
		//now the size of DMA can be set by register in the function setFPGAReg()
	 	//fpgadma = new FPGADma(this,dmaPort, MemoryRange*MemorySize, 64, 8,Request::UNCACHEABLE);
	}

    memset(inputArray,0,sizeof(inputArray));
    memset(outputArray,0,sizeof(outputArray));
}

void
FpgaCPU::reconfiguration()
{
	activateContext(0);
}

FpgaCPU::~FpgaCPU()
{
}

DrainState
FpgaCPU::drain()
{
    if (switchedOut())
        return DrainState::Drained;

    if (_status == Idle ||
        (_status == BaseSimpleCPU::Running && isDrained())) {
        DPRINTF(Drain, "No need to drain.\n");
        activeThreads.clear();
        return DrainState::Drained;
    } else {
        DPRINTF(Drain, "Requesting drain.\n");

        // The fetch event can become descheduled if a drain didn't
        // succeed on the first attempt. We need to reschedule it if
        // the CPU is waiting for a microcode routine to complete.
        if (_status == BaseSimpleCPU::Running && !fetchEvent.scheduled())
            schedule(fetchEvent, clockEdge());

        return DrainState::Draining;
    }
}

void
FpgaCPU::drainResume()
{
    assert(!fetchEvent.scheduled());
    if (switchedOut())
        return;

    DPRINTF(SimpleCPU, "Resume\n");
    verifyMemoryMode();

    assert(!threadContexts.empty());

    _status = BaseSimpleCPU::Idle;

    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (threadInfo[tid]->thread->status() == ThreadContext::Active) {
            threadInfo[tid]->notIdleFraction = 1;

            activeThreads.push_back(tid);

            _status = BaseSimpleCPU::Running;

            // Fetch if any threads active
            if (!fetchEvent.scheduled()) {
                schedule(fetchEvent, nextCycle());
            }
        } else {
            threadInfo[tid]->notIdleFraction = 0;
        }
    }

    system->totalNumInsts = 0;
}

bool
FpgaCPU::tryCompleteDrain()
{
    if (drainState() != DrainState::Draining)
        return false;

    DPRINTF(Drain, "tryCompleteDrain.\n");
    if (!isDrained())
        return false;

    DPRINTF(Drain, "CPU done draining, processing drain event\n");
    signalDrainDone();

    return true;
}

void
FpgaCPU::switchOut()
{
    SimpleExecContext& t_info = *threadInfo[curThread];
    M5_VAR_USED SimpleThread* thread = t_info.thread;
    BaseSimpleCPU::switchOut();

    assert(!fetchEvent.scheduled());
    assert(_status == BaseSimpleCPU::Running || _status == Idle);
    assert(!t_info.stayAtPC);
    assert(thread->microPC() == 0);

    updateCycleCounts();
}


void
FpgaCPU::takeOverFrom(BaseCPU *oldCPU)
{
    BaseSimpleCPU::takeOverFrom(oldCPU);

    previousCycle = curCycle();
}

void
FpgaCPU::verifyMemoryMode() const
{
    if (!system->isTimingMode()) {
        fatal("The timing CPU requires the memory system to be in "
              "'timing' mode.\n");
    }
}

#define FILEPATH_MAX (80)


string
FpgaCPU::extractModule(int id)
{
	int now=0;
	string ans="";
	if (id>1)
	for (now=0;now<moduleName.size();now++)
	{
		if (moduleName[now]==';') {now++;break;}
	}
	if (now==moduleName.size()) fatal("Fail to reconfigure the FPGA with bitstream %d\n",id);
	for (;now<moduleName.size();now++)
	{
		if (moduleName[now]==';') break;
		ans=ans+moduleName[now];
	}
	return ans;
}

void
FpgaCPU::activateContext(ThreadID thread_num)
{
	cout << "FpgaCPU--configuring" << endl;
	
    if (_status == BaseSimpleCPU::Idle)
        _status = BaseSimpleCPU::Running;

	RunState = 0;
    ReadReady = 0;
    WriteReady = 0;
    edge_RENA = 0;
    edge_WENA = 0;
    ReadEnable = 0;
    WriteEnable = 0;
	Terminate = 0;
	writeFault = 0;
	readFault = 0;
    inputArray[bit_Run] = 1;
	dma_read_done = 0;
	dma_read_begin = 0;
	dma_write_done = 0;
	dma_write_begin = 0;
	already_reset = 0;

    if (configured)
	{
            shared->text[num_input_fpga+num_output_fpga] = 10101;
			shared->written = 1;
		//	exitSimLoop("as the end of simulation, FPGA is terminated. exit()\n");
			waitpid(fpid,NULL,0);
			while ((kill(fpid,0)>=0)) shared->written = 1;
			printf("wipe the FPGA successfully.\n");
			deleteShare();
	}
	else
		configured = 1;

	createShare();
	shared = (struct shared_use_st*)shm;

	shared->text[num_input_fpga+num_output_fpga] = 0;
    shared->written = 0;
	//double tmp=frequency();tmp=tmp/300000000*((double)latency*312.558);
	//latency=tmp;
    //printf("frequency -- %lu\n",frequency());

	configuration_finished = OccupyFPGA;
    fpid = fork();

    if (fpid == 0)
    {
		string name;
		if (Reconfigurable)
			name = extractModule(OccupyFPGA);
		else
			name = moduleName;
		string tmp = "./fpga/" + name;
		cout << name << " is configured on FPGA." << endl;
        int rc=execl(tmp.c_str(),name.c_str(),(char*)0);
		tmp.~string();
		name.~string();
		if (rc == -1)
			fatal("fpga process can not work");
    }

    schedule(fetchEvent, clockEdge(Cycles(0)));

}


void
FpgaCPU::suspendContext(ThreadID thread_num)
{
    DPRINTF(SimpleCPU, "SuspendContext %d\n", thread_num);

    assert(thread_num < numThreads);
    activeThreads.remove(thread_num);

    if (_status == Idle)
        return;

    assert(_status == BaseSimpleCPU::Running);

    threadInfo[thread_num]->notIdleFraction = 0;

    if (activeThreads.empty()) {
        _status = Idle;

        if (fetchEvent.scheduled()) {
            deschedule(fetchEvent);
        }
    }

    BaseCPU::suspendContext(thread_num);
}

bool
FpgaCPU::handleReadPacket(PacketPtr pkt)
{

	//if (pkt->req->getVaddr()==140737488298048)
	{
		//printf("virtual address -- > %lu\n",pkt->req->getVaddr());
		//printf("physical address -- > %lu\n",pkt->req->getPaddr());
	}
    if (!dcachePort.sendTimingReq(pkt)) {
        _status = DcacheRetry;
        dcache_pkt = pkt;
    } else {
        _status = DcacheWaitResponse;
        // memory system takes ownership of packet
        dcache_pkt = NULL;
    }
    return dcache_pkt == NULL;
}

void
FpgaCPU::sendData(RequestPtr req, uint8_t *data, uint64_t *res,
                          bool read)
{
	PacketPtr pkt;
    pkt = buildPacket(req, read);
    pkt->dataDynamic<uint8_t>(data);
    if (req->getFlags().isSet(Request::NO_ACCESS)) {
        assert(!dcache_pkt);
        pkt->makeResponse();
        completeDataAccess(pkt);
    } else if (read) {
		if (dma_available)
		{
			if (dma_read_done) {completeDataAccess(pkt);}
			else
				fpgadma->startFill(pkt->req->getPaddr(),MemoryRange*MemorySize,0,pkt);
		}
        else
			handleReadPacket(pkt);
    } else {
        bool do_access = true;  // flag to suppress cache access
        if (do_access) {
            
			dcache_pkt = pkt;
			if (dma_available)
			{
				 completeDataAccess(pkt);
			}
		    else
           	 	handleWritePacket();
			//threadSnoop(pkt,InvalidThreadID);
        } else {
            _status = DcacheWaitResponse;
            completeDataAccess(pkt);
        }
    }
}

void
FpgaCPU::sendSplitData(RequestPtr req1, RequestPtr req2,
                               RequestPtr req, uint8_t *data, bool read)
{
	fatal("FpgaCPU::sendSplitData\n");
    PacketPtr pkt1, pkt2;
    buildSplitPacket(pkt1, pkt2, req1, req2, req, data, read);
    if (req->getFlags().isSet(Request::NO_ACCESS)) {
        assert(!dcache_pkt);
        pkt1->makeResponse();
        completeDataAccess(pkt1);
    } else if (read) {
        SplitFragmentSenderState * send_state =
            dynamic_cast<SplitFragmentSenderState *>(pkt1->senderState);
        if (handleReadPacket(pkt1)) {
            send_state->clearFromParent();
            send_state = dynamic_cast<SplitFragmentSenderState *>(
                    pkt2->senderState);
            if (handleReadPacket(pkt2)) {
                send_state->clearFromParent();
            }
        }
    } else {
        dcache_pkt = pkt1;
        SplitFragmentSenderState * send_state =
            dynamic_cast<SplitFragmentSenderState *>(pkt1->senderState);
        if (handleWritePacket()) {
            send_state->clearFromParent();
            dcache_pkt = pkt2;
            send_state = dynamic_cast<SplitFragmentSenderState *>(
                    pkt2->senderState);
            if (handleWritePacket()) {
                send_state->clearFromParent();
            }
        }
    }
}

void
FpgaCPU::translationFault(const Fault &fault)
{
	fatal("FpgaCPU::translationFault\n");
    updateCycleCounts();

    if (traceData) {
        // Since there was a fault, we shouldn't trace this instruction.
        delete traceData;
        traceData = NULL;
    }

    //postExecute();

    advanceInst(fault);
}

PacketPtr
FpgaCPU::buildPacket(RequestPtr req, bool read)
{
    return read ? Packet::createRead(req) : Packet::createWrite(req);
}

void
FpgaCPU::buildSplitPacket(PacketPtr &pkt1, PacketPtr &pkt2,
        RequestPtr req1, RequestPtr req2, RequestPtr req,
        uint8_t *data, bool read)
{
    pkt1 = pkt2 = NULL;

    assert(!req1->isMmappedIpr() && !req2->isMmappedIpr());

    if (req->getFlags().isSet(Request::NO_ACCESS)) {
        pkt1 = buildPacket(req, read);
        return;
    }

    pkt1 = buildPacket(req1, read);
    pkt2 = buildPacket(req2, read);

    PacketPtr pkt = new Packet(req, pkt1->cmd.responseCommand());

    pkt->dataDynamic<uint8_t>(data);
    pkt1->dataStatic<uint8_t>(data);
    pkt2->dataStatic<uint8_t>(data + req1->getSize());

    SplitMainSenderState * main_send_state = new SplitMainSenderState;
    pkt->senderState = main_send_state;
    main_send_state->fragments[0] = pkt1;
    main_send_state->fragments[1] = pkt2;
    main_send_state->outstanding = 2;
    pkt1->senderState = new SplitFragmentSenderState(pkt, 0);
    pkt2->senderState = new SplitFragmentSenderState(pkt, 1);
}

Fault
FpgaCPU::readMem(Addr addr, uint8_t *data,
                         unsigned size, Request::Flags flags)
{
    panic("readMem() is for atomic accesses, and should "
          "never be called on TimingSimpleCPU.\n");
}

Fault
FpgaCPU::initiateMemRead(Addr addr, unsigned size,
                          Request::Flags flags)
{
    Fault fault;
    const int asid = 0;
    ThreadID tid = CurrentThreadID;
    ThreadContext* CurrentTC = system->getThreadContext(tid);
    const Addr pc = 0;
    unsigned block_size = cacheLineSize();
    BaseTLB::Mode mode = BaseTLB::Read;
    if (traceData)
        traceData->setMem(addr, size, flags);

    RequestPtr req  = new Request(asid, addr, size,
                                  flags, dataMasterId(), pc, CurrentThreadID);//InvalidThreadID

	req->fromFPGA = 1;

    req->taskId(taskId());

    Addr split_addr = roundDown(addr + size - 1, block_size);
    assert(split_addr <= addr || split_addr - addr < block_size);

    _status = DTBWaitResponse;
    if (split_addr > addr) {
        RequestPtr req1, req2;
        assert(!req->isLLSC() && !req->isSwap());
        req->splitOnVaddr(split_addr, req1, req2);
		req1->fromFPGA = 1;
		req2->fromFPGA = 1;
        WholeTranslationState *state =
            new WholeTranslationState(req, req1, req2, new uint8_t[size],
                                      NULL, mode);
        DataTranslation<FpgaCPU *> *trans1 =
            new DataTranslation<FpgaCPU *>(this, state, 0);
        DataTranslation<FpgaCPU *> *trans2 =
            new DataTranslation<FpgaCPU *>(this, state, 1);

        CurrentTC->getDTBPtr()->translateTiming(req1, CurrentTC, trans1, mode);
        CurrentTC->getDTBPtr()->translateTiming(req2, CurrentTC, trans2, mode);
    } else {
        WholeTranslationState *state =
            new WholeTranslationState(req, new uint8_t[size], NULL, mode);
        DataTranslation<FpgaCPU *> *translation
            = new DataTranslation<FpgaCPU *>(this, state);
        CurrentTC->getDTBPtr()->translateTiming(req, CurrentTC, translation, mode);
    }


    return NoFault;
}

bool
FpgaCPU::handleWritePacket()
{
	//printf("virtual address -- > %lu\n",dcache_pkt->req->getVaddr());
	//printf("physical address -- > %lu\n",dcache_pkt->req->getPaddr());
    if (!dcachePort.sendTimingReq(dcache_pkt)) {
        _status = DcacheRetry;
    } else {
        _status = DcacheWaitResponse;
        // memory system takes ownership of packet
        dcache_pkt = NULL;
    }
    return dcache_pkt == NULL;
}

static inline uint64_t
getMem(PacketPtr pkt, unsigned dataSize, Trace::InstRecord *traceData)
{
    uint64_t mem;
    switch (dataSize)
    {
        case 1:
            mem = pkt->get<uint8_t>(); break;
        case 2:
            mem = pkt->get<uint16_t>(); break;
        case 4:
            mem = pkt->get<uint32_t>();break;
        case 8:
            mem = pkt->get<uint64_t>();break;
        default:
            fatal("Unhandled size in getMem.\n");
    }
    if (traceData)
        traceData->setData(mem);
    return mem;
}

Fault
FpgaCPU::writeMem(uint8_t *data, unsigned size, Addr addr, Request::Flags flags, uint64_t *res)
{
    uint8_t *newData = new uint8_t[size];
    const int asid = 0;
    ThreadID tid = CurrentThreadID;
	ThreadContext* CurrentTC = system->getThreadContext(tid);
    const Addr pc = 0;
    unsigned block_size = cacheLineSize();
    BaseTLB::Mode mode = BaseTLB::Write;
    if (data == NULL) {
        assert(flags & Request::CACHE_BLOCK_ZERO);
        // This must be a cache block cleaning request
        memset(newData, 0, size);
    } else {
        memcpy(newData, data, size);
    }

    if (traceData)
        traceData->setMem(addr, size, flags);

    RequestPtr req = new Request(asid, addr, size,
                                 flags, dataMasterId(), pc, InvalidThreadID);

	req->fromFPGA = 1;
    req->taskId(taskId());
    Addr split_addr = roundDown(addr + size - 1, block_size);
    assert(split_addr <= addr || split_addr - addr < block_size);
    _status = DTBWaitResponse;
    if (split_addr > addr) {
        RequestPtr req1, req2;
        assert(!req->isLLSC() && !req->isSwap());
        req->splitOnVaddr(split_addr, req1, req2);
		req1->fromFPGA = 1;
		req2->fromFPGA = 1;
        WholeTranslationState *state =
            new WholeTranslationState(req, req1, req2, newData, res, mode);
        DataTranslation<FpgaCPU *> *trans1 =
            new DataTranslation<FpgaCPU *>(this, state, 0);
        DataTranslation<FpgaCPU *> *trans2 =
            new DataTranslation<FpgaCPU *>(this, state, 1);

        CurrentTC->getDTBPtr()->translateTiming(req1, CurrentTC, trans1, mode);
        CurrentTC->getDTBPtr()->translateTiming(req2, CurrentTC, trans2, mode);
    } else {
        WholeTranslationState *state =
            new WholeTranslationState(req, newData, res, mode);
        DataTranslation<FpgaCPU *> *translation =
            new DataTranslation<FpgaCPU *>(this, state);
        CurrentTC->getDTBPtr()->translateTiming(req, CurrentTC, translation, mode);
    }
    return NoFault;
}

void
FpgaCPU::threadSnoop(PacketPtr pkt, ThreadID sender)
{
    for (ThreadID tid = 0; tid < numThreads; tid++) {
        if (tid != sender) {
            if (getCpuAddrMonitor(tid)->doMonitor(pkt)) {
                wakeup(tid);
            }
            TheISA::handleLockedSnoop(threadInfo[tid]->thread, pkt,
                    dcachePort.cacheBlockMask);
        }
    }
}

void
FpgaCPU::finishTranslation(WholeTranslationState *state)
{
	//if (tmp) printf("FPGA TRANSFINI\n");
    _status = BaseSimpleCPU::Running;

	if (0)//(ACP)
	{
    	assert(!dcachePort.acpSendEvent.scheduled());
    	// delay processing of returned data until next CPU clock edge
    	dcachePort.acpSendEvent.schedule(state, clockEdge(Cycles(5)));
		return;
	}

    if (state->getFault() != NoFault) {
        if (state->isPrefetch()) {
            state->setNoFault();
        }
        printf("virt addr -- %lu\n",state->mainReq->getVaddr());
		if (state->mode == BaseTLB::Read) readFault = 1;
		else writeFault = 1;
        delete [] state->data;
        state->deleteReqs();
        translationFault(state->getFault());
    } else {
    //    if (!state->isSplit) {
            sendData(state->mainReq, state->data, state->res,
                     state->mode == BaseTLB::Read);
		if(show_address && state->mode == BaseTLB::Read) printf("%s physical addr -- %lu\n",name().c_str(),state->mainReq->getPaddr());
   //     } else {
     //       sendSplitData(state->sreqLow, state->sreqHigh, state->mainReq,
       //                   state->data, state->mode == BaseTLB::Read);
      //  }
    }

    delete state;
}




void
FpgaCPU::fetch()  //FPGACPU-special==========================actually FPGA has no procedure of fetching but it can as a procedure of communicating with another process of verilator
{
    DPRINTF(SimpleCPU, "Fetch\n");

	if (Terminate == 1)
		{
            shared->text[num_input_fpga+num_output_fpga] = 10101;
			shared->written = 1;
			_status = Idle;
		//	exitSimLoop("as the end of simulation, FPGA is terminated. exit()\n");
			return;
        }
	else shared->text[num_input_fpga+num_output_fpga] = 0;
	if (kill(fpid,0)<0)
		exitSimLoop("The process of FPGA is terminated unexceptedly\n");
   	schedule(fetchEvent, clockEdge(Cycles(1)));// insert another procedure of fetch at next cycle.
	if (dma_available)
	{
		if (dma_write_begin&&fpgadma->all_done)
			dma_write_done=1;
	}
    if (_status == Idle)
        return;
	if (!OccupyFPGA) {return;}

    InputChanged = 0;
    OutputChanged = 0;
    while(shared->written == 1) {InputChanged = 0;};
	//get data from fpga process
    for (cnt=num_input_fpga;cnt<num_input_fpga+num_output_fpga;cnt++)
    {
        outputArray[cnt-num_input_fpga]=shared->text[cnt];
        if (outputArray[cnt-num_input_fpga] != outputArray_last[cnt-num_input_fpga])
            OutputChanged = 1;
    }
	
    for (cnt=0;cnt<num_input_fpga;cnt++)
    {
        shared->text[cnt]=inputArray[cnt];
        if (inputArray[cnt] != inputArray_last[cnt])
            InputChanged = 1;
    }
	//authorize the fpga process to change shared memory
	
	if (outputArray[bit_Done]) 
	{
		
                if (TaskHash && RunState)
                {
		        printf("FPGA released from TaskHash %lu\n", TaskHash);
		        TaskHash = 0;
                }
                RunState = 0;
		if (dma_available)
		{
			fpgadma->startFill(fpgadma->startAddr,MemoryRange*MemorySize,1,nullptr);
		}
	}
	shared->written = 1;
	//deal with the situation where fpga access cache and memory via virtual memory
    if (OutputChanged && RunState)
    {
		//ReadEnable
        if (outputArray_last[bit_ReadEnable] != outputArray[bit_ReadEnable])
        {
            ReadEnable = outputArray[bit_ReadEnable];
            if (outputArray_last[bit_ReadEnable] < outputArray[bit_ReadEnable]) 
			{
				edge_RENA = 1;
			//	printf("read detect\n");
			}
            else edge_RENA = 0;
        }
        else edge_RENA = 0;
        //WriteEnabel
        if (outputArray_last[bit_WriteEnable] != outputArray[bit_WriteEnable])
        {
            WriteEnable = outputArray[bit_WriteEnable];
            if (outputArray_last[bit_WriteEnable] < outputArray[bit_WriteEnable]) edge_WENA = 1;
            else edge_WENA = 0;
        }
        else edge_WENA = 0;
    }
	else {edge_RENA = 0;edge_WENA = 0;}

    for (cnt = 0; cnt < 16; cnt++)
        outputArray_last[cnt] = outputArray[cnt];
    for (cnt = 0; cnt < 16; cnt++)
        inputArray_last[cnt] = inputArray[cnt];
	if (inputArray[bit_ReadReady]||inputArray[bit_WriteReady]) tmp=0;
	inputArray[bit_ReadReady] = 0;
	inputArray[bit_WriteReady] = 0;
	//if (tmp) printf("FPGA CLK\n");
	//if (ReadEnable) printf("detect read %lu\n",outputArray[bit_ReadAddr]);
	
    if (((ReadEnable && (outputArray[bit_FinishRead] || edge_RENA || (Protocol_shakehand&&ReadReady))) || readFault)&&already_reset)
    {
		//tmp=1;
	//	printf("FPGA Req\n");
		if (readFault)
			fatal("readFault");
		readFault = 0;
        ReadReady = 0;
        inputArray[bit_ReadReady] = 0;
	//	if (outputArray[bit_ReadAddr]==140737488298048) printf("begin read\n");
        initiateMemRead(outputArray[bit_ReadAddr], outputArray[bit_Size_ReadData_Output],0);
    }
    else if (outputArray[bit_FinishRead]||!ReadEnable)
    {
		//if (outputArray[bit_ReadAddr]==140737488298048) printf("stop read\n");
        inputArray[bit_ReadReady] = 0;
        ReadReady = 0;
    }

    if (((WriteEnable && (outputArray[bit_FinishWrite] || edge_WENA)) || writeFault)&&already_reset)
    {
		//tmp=1;printf("FPGA Req\n");
		writeFault = 0;
        WriteReady = 0;
        inputArray[bit_WriteReady] = 0;
      	writeMem((uint8_t*)&outputArray[bit_WriteData], outputArray[bit_Size_WriteData],outputArray[bit_WriteAddr], 0, NULL);
    }
    else if (outputArray[bit_FinishWrite]||!WriteEnable)
    {
        inputArray[bit_WriteReady] = 0;
        WriteReady = 0;
    }
}


void
FpgaCPU::sendFetch(const Fault &fault, RequestPtr req,
                           ThreadContext *tc)
{
	fatal("FpgaCPU::sendFetch");
    if (fault == NoFault) {
        DPRINTF(SimpleCPU, "Sending fetch for addr %#x(pa: %#x)\n",
                req->getVaddr(), req->getPaddr());
        ifetch_pkt = new Packet(req, MemCmd::ReadReq);
        ifetch_pkt->dataStatic(&inst);
        DPRINTF(SimpleCPU, " -- pkt addr: %#x\n", ifetch_pkt->getAddr());

        if (!icachePort.sendTimingReq(ifetch_pkt)) {
            // Need to wait for retry
            _status = IcacheRetry;
        } else {
            // Need to wait for cache to respond
            _status = IcacheWaitResponse;
            // ownership of packet transferred to memory system
            ifetch_pkt = NULL;
        }
    } else {
        DPRINTF(SimpleCPU, "Translation of addr %#x faulted\n", req->getVaddr());
        delete req;
        // fetch fault: advance directly to next instruction (fault handler)
        _status = BaseSimpleCPU::Running;
        advanceInst(fault);
    }

    updateCycleCounts();
}


void
FpgaCPU::advanceInst(const Fault &fault)
{
	fatal("FpgaCPU::advanceInst");
    /*SimpleExecContext &t_info = *threadInfo[curThread];

    if (_status == Faulting)
        return;

    if (fault != NoFault) {
        advancePC(fault);
        DPRINTF(SimpleCPU, "Fault occured, scheduling fetch event\n");
        reschedule(fetchEvent, clockEdge(), true);
        _status = Faulting;
        return;
    }


    if (!t_info.stayAtPC)
        advancePC(fault);

    if (tryCompleteDrain())
            return;

    if (_status == BaseSimpleCPU::Running) {
        // kick off fetch of next instruction... callback from icache
        // response will cause that instruction to be executed,
        // keeping the CPU running.
        fetch();
    }*/
}


void
FpgaCPU::completeIfetch(PacketPtr pkt)
{
	fatal("FpgaCPU::advanceInst\n");
    SimpleExecContext& t_info = *threadInfo[curThread];

    DPRINTF(SimpleCPU, "Complete ICache Fetch for addr %#x\n", pkt ?
            pkt->getAddr() : 0);

    // received a response from the icache: execute the received
    // instruction
    assert(!pkt || !pkt->isError());
    assert(_status == IcacheWaitResponse);

    _status = BaseSimpleCPU::Running;

    updateCycleCounts();

    if (pkt)
        pkt->req->setAccessLatency();


    preExecute();
    if (curStaticInst && curStaticInst->isMemRef()) {
        // load or store: just send to dcache
        Fault fault = curStaticInst->initiateAcc(&t_info, traceData);

        // If we're not running now the instruction will complete in a dcache
        // response callback or the instruction faulted and has started an
        // ifetch
        if (_status == BaseSimpleCPU::Running) {
            if (fault != NoFault && traceData) {
                // If there was a fault, we shouldn't trace this instruction.
                delete traceData;
                traceData = NULL;
            }

            postExecute();
            // @todo remove me after debugging with legion done
            if (curStaticInst && (!curStaticInst->isMicroop() ||
                        curStaticInst->isFirstMicroop()))
                instCnt++;
            advanceInst(fault);
        }
    } else if (curStaticInst) {
        // non-memory instruction: execute completely now
        Fault fault = curStaticInst->execute(&t_info, traceData);

        // keep an instruction count
        if (fault == NoFault)
            countInst();
        else if (traceData && !DTRACE(ExecFaulting)) {
            delete traceData;
            traceData = NULL;
        }

        postExecute();
        // @todo remove me after debugging with legion done
        if (curStaticInst && (!curStaticInst->isMicroop() ||
                curStaticInst->isFirstMicroop()))
            instCnt++;
        advanceInst(fault);
    } else {
        advanceInst(NoFault);
    }

    if (pkt) {
        delete pkt->req;
        delete pkt;
    }
}

void
FpgaCPU::IcachePort::ITickEvent::process()
{
//cout << "FpgaCPU::IcachePort::ITickEvent::process()"  << endl;
    cpu->completeIfetch(pkt);
}

bool
FpgaCPU::IcachePort::recvTimingResp(PacketPtr pkt)
{
//cout << "FpgaCPU::IcachePort::recvTimingResp(PacketPtr pkt)"  << endl;
    DPRINTF(SimpleCPU, "Received fetch response %#x\n", pkt->getAddr());
    // we should only ever see one response per cycle since we only
    // issue a new request once this response is sunk
    assert(!tickEvent.scheduled());
    // delay processing of returned data until next CPU clock edge
    tickEvent.schedule(pkt, cpu->clockEdge());

    return true;
}

void
FpgaCPU::IcachePort::recvReqRetry()
{
//cout << "FpgaCPU::IcachePort::recvReqRetry()"  << endl;
    // we shouldn't get a retry unless we have a packet that we're
    // waiting to transmit
    assert(cpu->ifetch_pkt != NULL);
    assert(cpu->_status == IcacheRetry);
    PacketPtr tmp = cpu->ifetch_pkt;
    if (sendTimingReq(tmp)) {
        cpu->_status = IcacheWaitResponse;
        cpu->ifetch_pkt = NULL;
    }
}

void
FpgaCPU::completeDataAccess(PacketPtr pkt)
{
	//if (tmp) printf("FPGA ACCESSFINI\n");
    // received a response from the dcache: complete the load or store
    // instruction
    assert(!pkt->isError());
    assert(_status == DcacheWaitResponse || _status == DTBWaitResponse ||
           pkt->req->getFlags().isSet(Request::NO_ACCESS)||dma_available);

    pkt->req->setAccessLatency();

    updateCycleCounts();

    _status = BaseSimpleCPU::Running;

    //Fault fault = curStaticInst->completeAcc(pkt, this, traceData);
    Fault fault = NoFault;
  //  int yyyy;

	if (dma_available)
	{
		
		if (pkt->cmd == MemCmd::ReadReq)
		{
			dma_read_begin = 1;
			if (!dma_read_done) 
			{
				//printf("finish DMA read\n");
				dma_read_done = 1;
				int i;
				for (i=0;i<dma_size;i++)
					scratchM[i]=fpgadma->buffer[i];
			}
		    ReadReady = 1;
		    inputArray[bit_ReadReady] = 1;
		    inputArray[bit_ReadData] = 0;
			std::memcpy((uint8_t*)&inputArray[bit_ReadData],((uint8_t*)&scratchM)+(pkt->req->getPaddr()-fpgadma->startAddr),outputArray[bit_Size_ReadData_Output]);
			//printf("Read Data : %lu\n",outputArray[bit_ReadData]);
		}
		if (pkt->cmd == MemCmd::WriteReq )
		{
			if (!dma_write_begin)
			{	
				dma_write_begin = 1;
				fpgadma->startAddr = pkt->req->getPaddr();
			}
			std::memcpy(((uint8_t*)&scratchM)+(pkt->req->getPaddr()-fpgadma->startAddr),(uint8_t*)&outputArray[bit_WriteData],outputArray[bit_Size_WriteData]);
		    WriteReady = 1;
		    inputArray[bit_WriteReady] = 1;
		}
	}


    if (pkt->cmd == MemCmd::ReadResp)
    {
        ReadReady = 1;
        inputArray[bit_ReadReady] = 1;
        inputArray[bit_ReadData] = getMem(pkt, outputArray[bit_Size_ReadData_Output], traceData);
    }
    if (pkt->cmd == MemCmd::WriteResp )
    {
        WriteReady = 1;
        inputArray[bit_WriteReady] = 1;
	//	if (pkt->req->getVaddr()==140737488298048)
		//	printf("recv write response %lu\n",outputArray[bit_WriteData]);
    }

    delete pkt->req;
    delete pkt;
}

void
FpgaCPU::updateCycleCounts()
{
//cout << "updateCycleCounts" << curCycle() << endl;
    const Cycles delta(curCycle() - previousCycle);
    numCycles += delta;
    ppCycles->notify(delta);
    previousCycle = curCycle();
}

void
FpgaCPU::DcachePort::recvTimingSnoopReq(PacketPtr pkt)
{
    // X86 ISA: Snooping an invalidation for monitor/mwait
    for (ThreadID tid = 0; tid < cpu->numThreads; tid++) {
        if (cpu->getCpuAddrMonitor(tid)->doMonitor(pkt)) {
            cpu->wakeup(tid);
        }
    }

    // Making it uniform across all CPUs:
    // The CPUs need to be woken up only on an invalidation packet (when using caches)
    // or on an incoming write packet (when not using caches)
    // It is not necessary to wake up the processor on all incoming packets
    if (pkt->isInvalidate() || pkt->isWrite()) {
        for (auto &t_info : cpu->threadInfo) {
            TheISA::handleLockedSnoop(t_info->thread, pkt, cacheBlockMask);
        }
    }
}

void
FpgaCPU::DcachePort::recvFunctionalSnoop(PacketPtr pkt)
{
    // X86 ISA: Snooping an invalidation for monitor/mwait
    for (ThreadID tid = 0; tid < cpu->numThreads; tid++) {
        if (cpu->getCpuAddrMonitor(tid)->doMonitor(pkt)) {
            cpu->wakeup(tid);
        }
    }
}

bool
FpgaCPU::DcachePort::recvTimingResp(PacketPtr pkt)
{
	if (cpu->tmp) printf("FPGA RESP\n");
    DPRINTF(SimpleCPU, "Received load/store response %#x\n", pkt->getAddr());

    // The timing CPU is not really ticked, instead it relies on the
    // memory system (fetch and load/store) to set the pace.
	if (0)//(cpu->ACP)
	{
		if (!acpRecvEvent.scheduled()) {
		    // Delay processing of returned data until next CPU clock edge
		    acpRecvEvent.schedule(pkt, cpu->clockEdge(Cycles(5)));
		    return true;
		} else {
		    // In the case of a split transaction and a cache that is
		    // faster than a CPU we could get two responses in the
		    // same tick, delay the second one
			printf("WARNING: not ready to recv\n");
		    if (!retryRespEvent.scheduled())
		        cpu->schedule(retryRespEvent, cpu->clockEdge(Cycles(1)));
		    return false;
		}	
	}
	else
	{
		if (!tickEvent.scheduled()) {
		    // Delay processing of returned data until next CPU clock edge
		  //  tickEvent.schedule(pkt, cpu->clockEdge());
			cpu->completeDataAccess(pkt);
		    return true;
		} else {
		    // In the case of a split transaction and a cache that is
		    // faster than a CPU we could get two responses in the
		    // same tick, delay the second one
			printf("WARNING: not ready to recv\n");
		    if (!retryRespEvent.scheduled())
		        cpu->schedule(retryRespEvent, cpu->clockEdge(Cycles(1)));
		    return false;
		}
	}
}

void
FpgaCPU::DcachePort::DTickEvent::process()
{
//cout << "FpgaCPU::DcachePort::DTickEvent::process()"  << endl;
    cpu->completeDataAccess(pkt);
}

void
FpgaCPU::DcachePort::ACPRecvEvent::process()
{
//cout << "FpgaCPU::DcachePort::DTickEvent::process()"  << endl;
    cpu->completeDataAccess(pkt);
}

void
FpgaCPU::DcachePort::ACPSendEvent::process()
{
//cout << "FpgaCPU::DcachePort::DTickEvent::process()"  << endl;
    if (state->getFault() != NoFault) {
        if (state->isPrefetch()) {
            state->setNoFault();
        }
        printf("virt addr -- %lu\n",state->mainReq->getVaddr());
		if (state->mode == BaseTLB::Read) cpu->readFault = 1;
		else cpu->writeFault = 1;
        delete [] state->data;
        state->deleteReqs();
        cpu->translationFault(state->getFault());
    } else {
            cpu->sendData(state->mainReq, state->data, state->res,
                     state->mode == BaseTLB::Read);
    }

    delete state;
}





void
FpgaCPU::DcachePort::recvReqRetry()
{
    // we shouldn't get a retry unless we have a packet that we're
    // waiting to transmit
    assert(cpu->dcache_pkt != NULL);
    assert(cpu->_status == DcacheRetry);
    PacketPtr tmp = cpu->dcache_pkt;
    if (tmp->senderState) {
        // This is a packet from a split access.
        SplitFragmentSenderState * send_state =
            dynamic_cast<SplitFragmentSenderState *>(tmp->senderState);
        assert(send_state);
        PacketPtr big_pkt = send_state->bigPkt;

        SplitMainSenderState * main_send_state =
            dynamic_cast<SplitMainSenderState *>(big_pkt->senderState);
        assert(main_send_state);

        if (sendTimingReq(tmp)) {
            // If we were able to send without retrying, record that fact
            // and try sending the other fragment.
            send_state->clearFromParent();
            int other_index = main_send_state->getPendingFragment();
            if (other_index > 0) {
                tmp = main_send_state->fragments[other_index];
                cpu->dcache_pkt = tmp;
                if ((big_pkt->isRead() && cpu->handleReadPacket(tmp)) ||
                        (big_pkt->isWrite() && cpu->handleWritePacket())) {
                    main_send_state->fragments[other_index] = NULL;
                }
            } else {
                cpu->_status = DcacheWaitResponse;
                // memory system takes ownership of packet
                cpu->dcache_pkt = NULL;
            }
        }
    } else if (sendTimingReq(tmp)) {
        cpu->_status = DcacheWaitResponse;
        // memory system takes ownership of packet
        cpu->dcache_pkt = NULL;
    }
}

FpgaCPU::IprEvent::IprEvent(Packet *_pkt, FpgaCPU *_cpu,
    Tick t)
    : pkt(_pkt), cpu(_cpu)
{
    cpu->schedule(this, t);
}

void
FpgaCPU::IprEvent::process()
{
    cpu->completeDataAccess(pkt);
}

const char *
FpgaCPU::IprEvent::description() const
{
    return "Timing Simple CPU Delay IPR event";
}


void
FpgaCPU::printAddr(Addr a)
{
    dcachePort.printAddr(a);
}


Tick
FpgaCPU::recvAtomic(PacketPtr pkt)
{
	Addr offset = (pkt->getAddr() - ControlAddr)>>3;
	uint64_t reg = offset;
	uint64_t val = htog(getFPGAReg(reg));
	if (pkt->isRead())
	{
	//	printf("isRead\n");
		pkt->setData((uint8_t *)&val);
		pkt->makeResponse();
	}
	else if (pkt->isWrite())
	{
		//printf("isWrite\n");
		pkt->writeData((uint8_t *)&val);
		setFPGAReg(reg,val,pkt);
		pkt->makeResponse();
	}
	return latency;
}

void 
FpgaCPU::recvFunctional(PacketPtr pkt)
{
	recvAtomic(pkt);
}

bool
FpgaCPU::recvTimingReq(PacketPtr pkt)
{
        //printf("FPGA recv REQ @ addr %lu\n",pkt->req->getVaddr());
    for (int x = 0; x < pendingDelete.size(); x++)
        delete pendingDelete[x];
    pendingDelete.clear();
/*
    if (retryReq)
        {
                printf("FPGA on retry\n");
        return false;
        }

    if (isBusy) {
                printf("FPGA begins retry\n");
        retryReq = true;
        return false;
    }

    pkt->headerDelay = pkt->payloadDelay = 0;

    if (pkt->isRead() || pkt->isWrite()) {

        Tick duration = latency;
        if (duration != 0) {
            schedule(releaseEvent, curTick() + duration);
           // isBusy = true;
        }
    }
*/
    bool needsResponse = pkt->needsResponse();
    recvAtomic(pkt);
    if (needsResponse) {
        assert(pkt->isResponse());
        packetQueue.emplace_back(DeferredPacket(pkt, curTick() + latency + pkt->headerDelay + pkt->payloadDelay));
        if (!retryResp && !dequeueEvent.scheduled())
            schedule(dequeueEvent, packetQueue.back().tick);
    } else {
                pendingDelete.push_back(pkt);
                        if (dma_available)
                        {
                                if (fpgadma!=nullptr) delete fpgadma;
	 			fpgadma = new FPGADma(this,dmaPort, MemoryRange*MemorySize, 64, 8,Request::UNCACHEABLE);
			}
    }

    return true;
}

void
FpgaCPU::release()
{
    assert(isBusy);
    isBusy = false;
    if (retryReq) {
        retryReq = false;
        controlPort.sendRetryReq();
    }
}

void
FpgaCPU::dequeue()
{
    assert(!packetQueue.empty());
    DeferredPacket deferred_pkt = packetQueue.front();

    retryResp = !controlPort.sendTimingResp(deferred_pkt.pkt);

    if (!retryResp) {
        packetQueue.pop_front();

        if (!packetQueue.empty()) {
            reschedule(dequeueEvent,
                       std::max(packetQueue.front().tick, curTick()), true);
        }
    }
}

void
FpgaCPU::recvRespRetry()
{
    assert(retryResp);
    dequeue();
}

BaseSlavePort &
FpgaCPU::getSlavePort(const std::string &if_name, PortID idx)
{
    if (if_name != "control_port") {
        return MemObject::getSlavePort(if_name, idx);
    } else {
        return controlPort;
    }
}

BaseMasterPort &
FpgaCPU::getMasterPort(const std::string &if_name, PortID idx)
{
    if (if_name == "dma") {
        return dmaPort;
    }
	else if (if_name == "dcache_port")
        return getDataPort();
    else if (if_name == "icache_port")
        return getInstPort();
    else
    return MemObject::getMasterPort(if_name, idx);
}

AddrRangeList
FpgaCPU::FpgaCPUControlPort::getAddrRanges() const
{
	AddrRange range(ControlAddr,ControlAddr+size_control_fpga);
    AddrRangeList ranges;
    ranges.push_back(range);
    return ranges;
}

void
FpgaCPU::setFPGAReg(uint64_t regid, uint64_t val, PacketPtr pkt)
{
//	printf("    FPGA regid  %lu\n",regid);
//	printf("    FPGA val    %lu\n",val);

    switch (regid)
    {
		case 0: // TaskHash=val;
                        if (!TaskHash&&val) {TaskHash=val;printf("FPGA occupied by TaskHash %lu\n",val);}
                        else printf("Reject FPGA TaskHash id %lu, currently FPGA occupied by TaskHash %lu\n",val, TaskHash);
				break;
        case 1: ReadBase = val;inputArray[bit_ReadBase] = val; break;
		case 2: WriteBase = val;inputArray[bit_WriteBase] = val; break;
        case 3: 
				{

					CurrentThreadID = pkt->req->contextId();
                    
					break;
				}
        case 4: MemoryRange = val; inputArray[bit_Num_Read]=val;break;
        case 5: MemorySize = val; inputArray[bit_Size_ReadData_Input] = val;break;
        case 6: 
				{
					RunState = val;inputArray[bit_Run] = (val==0); 
					if(!RunState) {printf("Reset FPGA @ tick %ld\n",curTick());	already_reset=1;}
					dma_read_done=0;dma_write_done=0;dma_read_begin=0;dma_write_begin=0;
					break;
				}
		case 7: Terminate = val;break;
		case 8: {OccupyFPGA = val;printf("occupy and configure FPGA with bitstream %lu\n",val);break;}
		case 9: fatal("The register ReturnValue can be set by only FPGA but not CPU\n");/*ReturnValue = val;*/break;
		case 10: inputArray[bit_In0]=val;break;
		case 11: inputArray[bit_In1]=val;break;
    }
	if (regid==4||regid==5)
	{
			if (dma_available)
			{	
				if (fpgadma!=nullptr) delete fpgadma;
	 			fpgadma = new FPGADma(this,dmaPort, MemoryRange*MemorySize, 64, 8,Request::UNCACHEABLE);
			}
	}	
	if (/*_status == BaseSimpleCPU::Idle || */(OccupyFPGA>0&&regid==8))
	{
		if (fetchEvent.scheduled()) deschedule(fetchEvent);
		configuration_finished=0;
		if (!Reconfigurable)
			activateContext(0);
		else
			schedule(reconfigurationEvent, curTick()+Reconfiguration_time);
	}
}

uint64_t
FpgaCPU::getFPGAReg(uint64_t regid)
{
	if (regid == 0) {return TaskHash;}
    else if  (regid == 1) {return ReadBase;}
	else if (regid == 2) {return WriteBase;}
    else if (regid == 3) {return CurrentThreadID;}
    else if (regid == 4) {return MemoryRange;}
    else if (regid == 5) {return MemorySize;}
    else if (regid == 6) {return RunState;}
	else if (regid == 7) {return Terminate;}
	else if (regid == 8) {return OccupyFPGA;}
	else if (regid == 9) {return outputArray[bit_ReturnValue];}	
	else if (regid ==10) {return dma_write_done;}
	else if (regid ==11) {return configuration_finished;}
	else if (regid == 10 || regid == 11) {/*printf("WARNING: attempt to read the IO INPUT (%lu) OF FPGA\n",regid);*/return inputArray[regid-8];}	
    else return 0;
}

Tick
FpgaCPU::FpgaCPUControlPort::recvAtomic(PacketPtr pkt)
{
    return cpu->recvAtomic(pkt);
}

void
FpgaCPU::FpgaCPUControlPort::recvFunctional(PacketPtr pkt)
{
    cpu->recvFunctional(pkt);
}

bool
FpgaCPU::FpgaCPUControlPort::recvTimingReq(PacketPtr pkt)
{
    return cpu->recvTimingReq(pkt);
}

void
FpgaCPU::FpgaCPUControlPort::recvRespRetry()
{
    cpu->recvRespRetry();
}

FpgaCPU *
FpgaCPUParams::create()
{
    numThreads = 1;
    return new FpgaCPU(this);
}
