%default {"instr":"","suff":""}
    movzx       rINSTbl,%ecx            # ecx<- A+
    andl        $$0xf,%ecx               # ecx<- A
    movs${suff}      (rFP, %ecx, 4), %xmm0   # %xmm0 <- 1st src
    sarl        $$4,rINST               # rINST<- B
    ${instr}${suff}      (rFP,rINST,4), %xmm0
    FETCH_INST_OPCODE 1 %eax
    movs${suff}      %xmm0, (rFP, %ecx, 4)    # vAA<- %xmm0
    ADVANCE_PC 1
    GOTO_NEXT_R %eax
