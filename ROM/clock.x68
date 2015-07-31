*-----------------------------------------------------------
* Title      :
* Written by :
* Date       :
* Description:
*-----------------------------------------------------------
    ORG    $400000
    DC.L    0
    DC.L    START

vBase EQU $EFE1FE 
vBufB EQU 512*0
vDirB EQU 512*2 

rTCData EQU 0
rTCClk EQU 1
rTCEnb EQU 2

D0 DC.B %00000000, %00011000, %00100100, %00100100, %00100100, %00100100, %00100100, %00011000, %00000000
D1 DC.B %00000000, %00001000, %00011000, %00001000, %00001000, %00001000, %00001000, %00011100, %00000000  
D2 DC.B %00000000, %00011000, %00100100, %00000100, %00001000, %00010000, %00100000, %00111100, %00000000
D3 DC.B %00000000, %00111100, %00001000, %00010000, %00111000, %00000100, %00000100, %00111000, %00000000
D4 DC.B %00000000, %00000100, %00001100, %00010100, %00100100, %00111110, %00000100, %00000100, %00000000
D5 DC.B %00000000, %00111100, %00100000, %00100000, %00111000, %00000100, %00000100, %00111000, %00000000
D6 DC.B %00000000, %00011100, %00100000, %00100000, %00111000, %00100100, %00100100, %00011000, %00000000
D7 DC.B %00000000, %00111100, %00000100, %00001000, %00010000, %00100000, %00100000, %00100000, %00000000
D8 DC.B %00000000, %00011000, %00100100, %00100100, %00011000, %00100100, %00100100, %00011000, %00000000
D9 DC.B %00000000, %00011000, %00100100, %00100100, %00011100, %00000100, %00000100, %00111000, %00000000
DA DC.B %00000000, %00001000, %00010100, %00010100, %00100010, %00111110, %00100010, %00100010, %00000000
DB DC.B %00000000, %00111000, %00100100, %00100100, %00111000, %00100100, %00100100, %00111000, %00000000
DC DC.B %00000000, %00011100, %00100000, %00100000, %00100000, %00100000, %00100000, %00011100, %00000000
DD DC.B %00000000, %00111000, %00100100, %00100100, %00100100, %00100100, %00100100, %00111000, %00000000
DE DC.B %00000000, %00111100, %00100000, %00100000, %00111000, %00100000, %00100000, %00111100, %00000000
DF DC.B %00000000, %00111100, %00100000, %00100000, %00111000, %00100000, %00100000, %00100000, %00000000

 START: 
    move.l #$610000, sp
    *move.l #HANDLER, $64

    bclr #rTCEnb, vBase+vBufB
    move.b #%01000101, d0
    jsr SEND
    move.b #%11111111, d0
    jsr SEND

	move.b #%11000101, d0
    jsr SEND
    jsr RECEIVE
    move.l #28, d2  
    move.l  #$1A700, a0
    jsr PRINT

    *READ:
    *stop #$2000
    *bra READ

    bset #rTCEnb, vBase+vBufB
     
    END_LABEL:
    bra END_LABEL

HANDLER:
	move.b #%00000001, d0
    jsr SEND
    jsr RECEIVE
    move.l #28, d2  
    move.l  #$1A700, a0
    jsr PRINT
    rte
   
 SEND:
    move.b #7, d2
    ITER_SEND:
    move.b d0, d1
    lsr.b d2, d1    
    and.b #$01, d1
    bclr #rTCClk, vBase+vBufB
    and.b #$FE, vBase+vBufB
    or.b d1, vBase+vBufB
    bset #rTCClk, vBase+vBufB
    cmp.b #0, d2
    sub.b #1, d2
    bge ITER_SEND
    rts

 RECEIVE: *not work
    move.b #0, d0 
    move.b #7, d2
    ITER_RECEIVE:
    lsl.b #1, d0
    bclr #rTCClk, vBase+vBufB
    move.b (vBase+vBufB), d1
    bset #rTCClk, vBase+vBufB
    and.b #$01, d1
    or.b d1, d0
    cmp.b #0, d2
    sub.b #1, d2
    bge ITER_RECEIVE
    rts

PRINT:
    move.l d0, d1
    lsr.l d2, d1 
    and.l #$0000000F, d1
    jsr CHECK 
    CHECK_END:
    sub.l #4, d2
    cmp #0, d2
    bge PRINT
    rts
CHECK:
    move.l #D0, a1
    mulu #9, d1
    add.l d1, a1 
    move.l #0, d1
    ITER:    
    move.b (a1)+, (a0)
    add.l #64, a0
    add.l #1, d1
    cmp #8, d1
    bne ITER
    sub.l  #511, a0
    rts
        
 END    START






































*~Font name~Courier New~
*~Font size~10~
*~Tab type~1~
*~Tab size~4~
