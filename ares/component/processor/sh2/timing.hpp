//CD32X timing (MicroNut99 ares fork): SH-2 execution cycles that ares counted as 1.
//Off (all 0) unless a system enables it (M32X::loadTiming, md/m32x/bus-internal.cpp: ARES_32X_TIMING cpu=1).
//Hitachi SH7604 hardware manual, appendix "instruction execution cycles":
//  BRA/BSR/JMP/JSR/RTS/BRAF/BSRF 2, RTE 4, TRAPA 8, BT/BF taken 3 (not taken 1), BT/S BF/S taken 2,
//  MUL.L/DMULS.L/DMULU.L/MAC.W/MAC.L 2 (to 4), TAS.B 4, AND/OR/XOR/TST.B #imm,@(R0,GBR) 3,
//  LDC.L @Rm+,SR/GBR/VBR 3, STC.L 2, SLEEP 3; a register loaded from memory and used by the
//  next instruction: +1 (load-use stall); STS MACH/MACL right after a multiply: +1.
struct SH2Timing {
  bool enabled = false;
  u8 branch = 1, rte = 3, trapa = 7, condTaken = 2, condTakenS = 1;
  u8 mul = 1, tas = 3, gbrOp = 2, ldcl = 2, stcl = 1;
  u8 loadUse = 1, macRead = 1;
  //the CD32X timing probe (console, 28 Oct 2026): MUL.L right after a MUL.L waits for the multiplier
  //(3.8 clocks each back to back: 2, then 4); of two loads in a row, every second one waits a clock for the
  //cache, which an instruction fetch is using (a cache hit costs 1.5 back to back)
  u8 mulPair = 0, loadAlt = 0;
  u8 extra[65536] = {};

  auto build() -> void {
    for(u32 op = 0; op < 65536; op++) extra[op] = enabled ? compute(op) : 0;
  }

  auto compute(u32 op) const -> u8 {
    u32 hi = op >> 12, lo = op & 0xff, lo4 = op & 0xf;
    if(hi == 0xa || hi == 0xb) return branch;                         //BRA, BSR
    if(hi == 0x4 && (lo == 0x2b || lo == 0x0b)) return branch;        //JMP, JSR
    if(op == 0x000b) return branch;                                    //RTS
    if(hi == 0x0 && (lo == 0x23 || lo == 0x03)) return branch;        //BRAF, BSRF
    if(op == 0x002b) return rte;                                       //RTE
    if((op & 0xff00) == 0xc300) return trapa;                          //TRAPA
    if(hi == 0x0 && lo4 == 0x7) return mul;                            //MUL.L
    if(hi == 0x3 && (lo4 == 0xd || lo4 == 0x5)) return mul;           //DMULS.L, DMULU.L
    if(hi == 0x4 && lo4 == 0xf) return mul;                            //MAC.W
    if(hi == 0x0 && lo4 == 0xf) return mul;                            //MAC.L
    if(hi == 0x4 && lo == 0x1b) return tas;                            //TAS.B
    if((op & 0xfc00) == 0xcc00) return gbrOp;                          //TST/AND/XOR/OR.B #imm,@(R0,GBR)
    if(hi == 0x4 && (lo == 0x07 || lo == 0x17 || lo == 0x27)) return ldcl;  //LDC.L @Rm+,SR/GBR/VBR
    if(hi == 0x4 && (lo == 0x03 || lo == 0x13 || lo == 0x23)) return stcl;  //STC.L SR/GBR/VBR,@-Rn
    if(op == 0x001b) return 2;                                         //SLEEP
    return 0;
  }

  //the register a load writes (0-15), or -1
  static auto loadDest(u32 op) -> s32 {
    u32 hi = op >> 12, n = op >> 8 & 15, lo4 = op & 0xf;
    if(hi == 0x6 && (lo4 <= 2 || (lo4 >= 4 && lo4 <= 6))) return n;  //MOV.x @Rm(+),Rn
    if(hi == 0x5) return n;                                            //MOV.L @(disp,Rm),Rn
    if(hi == 0x0 && lo4 >= 0xc && lo4 <= 0xe) return n;                //MOV.x @(R0,Rm),Rn
    if(hi == 0x9 || hi == 0xd) return n;                               //MOV.W/L @(disp,PC),Rn
    if((op & 0xfe00) == 0x8400) return 0;                              //MOV.B/W @(disp,Rm),R0
    if((op & 0xfc00) == 0xc400 && (op & 0xff00) != 0xc700) return 0;   //MOV.x @(disp,GBR),R0
    return -1;
  }

  //does op read register r?  (CD32X probe, 28 Oct 2026: the first version took the n field of the 0x6 loads and
  //moves, and R0 of the 0x84 / 0xC4 loads, as read - back to back loads into one register paid a load-use stall)
  static auto reads(u32 op, s32 r) -> bool {
    u32 hi = op >> 12, n = op >> 8 & 15, m = op >> 4 & 15, lo4 = op & 0xf, hb = op >> 8;
    switch(hi) {
    case 0x0:
      if(lo4 >= 4 && lo4 <= 6) return (s32)n == r || (s32)m == r || r == 0;   //MOV.x Rm,@(R0,Rn)
      if(lo4 == 7 || lo4 == 0xf) return (s32)n == r || (s32)m == r;           //MUL.L, MAC.L
      if(lo4 >= 0xc && lo4 <= 0xe) return (s32)m == r || r == 0;              //MOV.x @(R0,Rm),Rn
      if(lo4 == 3) return (s32)n == r;                                         //BSRF, BRAF
      return false;
    case 0x1: case 0x2: case 0x3: return (s32)n == r || (s32)m == r;
    case 0x4: return (s32)n == r || (lo4 == 0xf && (s32)m == r);               //MAC.W reads both
    case 0x5: case 0x6: return (s32)m == r;                                    //loads and moves write Rn
    case 0x7: return (s32)n == r;
    case 0x8:
      if(hb == 0x80 || hb == 0x81) return r == 0 || (s32)m == r;               //MOV.x R0,@(disp,Rn)
      if(hb == 0x84 || hb == 0x85) return (s32)m == r;                         //MOV.x @(disp,Rm),R0
      if(hb == 0x88) return r == 0;                                            //CMP/EQ #imm,R0
      return false;
    case 0xc:
      if(hb <= 0xc2 || hb >= 0xc8) return r == 0;                              //R0 stores, #imm ops on R0 / @(R0,GBR)
      return false;
    }
    return false;
  }

  static auto isMul(u32 op) -> bool {
    u32 hi = op >> 12, lo4 = op & 0xf;
    return (hi == 0x0 && (lo4 == 0x7 || lo4 == 0xf)) || (hi == 0x2 && (lo4 == 0xe || lo4 == 0xf))
        || (hi == 0x3 && (lo4 == 0xd || lo4 == 0x5)) || (hi == 0x4 && lo4 == 0xf);   //MUL.L MAC.L MULU MULS DMUL MAC.W
  }
  static auto isLoad(u32 op) -> bool { return loadDest(op) >= 0; }

  auto pair(u32 prev, u32 op) const -> u8 {
    if(!enabled) return 0;
    u8 c = 0;
    if(mulPair && isMul(prev) && isMul(op)) c += mulPair;
    s32 d = loadDest(prev);
    if(d >= 0 && loadUse && reads(op, d)) c += loadUse;
    bool prevMul = (prev >> 12 == 0x0 && (prev & 0xf) == 0x7) || (prev >> 12 == 0x2 && ((prev & 0xf) == 0xe || (prev & 0xf) == 0xf))
                || (prev >> 12 == 0x3 && ((prev & 0xf) == 0xd || (prev & 0xf) == 0x5));
    if(prevMul && macRead && (op & 0xf0ef) == 0x000a) c += macRead;  //STS MACH/MACL,Rn
    return c;
  }
};
