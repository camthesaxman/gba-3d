    .syntax unified
    .arm

    .section	.iwram,"ax",%progbits

    .set BG_COLOR, 251

    .set SCREEN_WIDTH,  240
    .set SCREEN_HEIGHT, 160

    .set SWI_DIV,        0x06
    .set SWI_CPUSET,     0x0B
    .set SWI_CPUFASTSET, 0x0C

    .set CPUSET_32BIT,     (1 << 26)
    .set CPUSET_SRC_FIXED, (1 << 24)

@ TODO: find a way to make sure these offsets are correct
    .set o_camera_x,      0x00
    .set o_camera_y,      0x04
    .set o_camera_height, 0x08
    .set o_camera_horizon, 0x0C
    .set o_camera_sinYaw, 0x10
    .set o_camera_cosYaw, 0x14

@ Assembly-optimized renderer
    .global render_asm
render_asm:
    push {r4-r12,lr}
    sub sp, sp, #(SCREEN_WIDTH/2+4)
    @ sp = ybuffer
    @ z will be stored above this on the stack
    .set local_saved_z, (SCREEN_WIDTH/2)

    @@@ Fill screen with BG color @@@

    ldr r12, =frameBuffer
    ldr r1, [r12]                @ r1 = dest address (frameBuffer)
    adr r0, bgColorFillValue    @ r0 = src address
    ldr r2, =(CPUSET_SRC_FIXED | (SCREEN_WIDTH * SCREEN_HEIGHT / 4))   @ r2 = control and size
    swi (SWI_CPUFASTSET << 16)

    @@@ Initialize y buffer @@@

    mov r1, sp                  @ r1 = dest address (ybuffer)
    adr r0, yBufferFillValue    @ r0 = src address
    ldr r2, =(CPUSET_SRC_FIXED | CPUSET_32BIT | (SCREEN_WIDTH/2/4))   @ r2 = control and size
    swi (SWI_CPUSET << 16)

    ldr r0, [r12]   @ r0 = frameBuffer

    @@@ Draw image

    mov r1, #1          @ r1 = z
  .LnextZ:
    ldr r2, =camera
    ldr r5, [r2, #o_camera_sinYaw]
    mul r3, r5, r1      @ r3 = camera.sinYaw * z
    ldr r5, [r2, #o_camera_cosYaw]
    mul r4, r5, r1      @ r4 = camera.cosYaw * z

    sub r5, r3, r4      @ r5 = ly   (sin * z - cos * z)
    neg r6, r5          @ r6 = rx   (cos * z - sin * z)
    neg r8, r4          @ -cos * z
    sub r7, r8, r3      @ r7 = lx   (-cos * z - sin * z)
    neg r8, r3          @ -sin * z
    sub r8, r4          @ r8 = ry   (-sin * z - cos * z)

    sub r6, r6, r7      @ r6 = dx   (rx - lx)
    sub r8, r8, r5      @ r8 = dy   (ry - ly)

    @ r3 and r4 are now free

    @ We should really divide them by the screen width (240), but dividing them
    @ by 256 is close enough. It just ends up shrinking the FOV slightly.
    asr r6, r6, #7      @ dx = (dx / 256) * 2
    asr r8, r8, #7      @ dy = (dy / 256) * 2

    ldr r3, [r2, #o_camera_x]
    add r7, r7, r3      @ lx += camera.x
    ldr r3, [r2, #o_camera_y]
    add r5, r5, r3      @ ly += camera.y

    @ compute 1 / z in fixed point
    ldr r9, =inverseTable
    ldr r9, [r9, r1, lsl #2]    @ r9 = (1 << 16) / z

    str r1, [sp, #local_saved_z]            @ store z onto the stack since it's not needed in the inner loop

    ldr r14, [r2, #o_camera_height]
    ldr r1, [r2, #o_camera_horizon]
    
    @ r2 is now free

    @ Draw columns

    mov r10, #0         @ r10 = i

    mov r2, #2048
    sub r2, #2          @ r2 = (1024 << 1)

  .LnextColumn:

    @ compute map index (r3)
    and r3, r2, r5, asr 15
    and r4, r2, r7, asr 15
    add r3, r4, r3, lsl 10      @ r3 = index

    @ compute height (r4)
    ldr r12, =terrain_bin
    ldrh r3, [r12, r3]          @ read terrain (heightmap value in upper byte, colormap value in lower byte)
    sub r4, r14, r3, lsr #8
    mul r12, r4, r9             @ r12 = (camera.height - heightmapBitmap[index]) * invz
    adds r4, r1, r12, asr #9   @ r4 = ((128 * (camera.height - heightmapBitmap[index]) * invz) >> 16) + camera.horizon;

    movlt r4, #0                @ if (height < 0) height = 0

    @ r12 is now free

    ldrb r11, [sp, r10]
    subs r11, r11, r4           @ r11 = ybuffer[i] - height
    ble .LskipBar               @ only draw if ybuffer[i] > height

    @@@ Draw vertical bar from coordinate (i, height) to (i, ybuffer[i]) @@@

    strb r4, [sp, r10]          @ update ybuffer[i]

    @ get color (r3)
    and r3, r3, #0xFF
    orr r3, r3, r3, lsl #8      @ r3 = color | (color << 8)

    @ compute dest (r12)
    rsb r12, r4, r4, lsl #4
    add r12, r10, r12, lsl #3      @ height * (SCREEN_WIDTH/2) + i
    add r12, r0, r12, lsl #1       @ r12 = dest

.if 0

  .LwriteBarPixel:
    strh r3, [r12], #SCREEN_WIDTH
    subs r11, #1
    bgt .LwriteBarPixel

.else

    @ Simple "Duff's Device" to optimize this innermost loop
    .set ITERATIONS_PER_LOOP, 16    @ must be a power of two
    and r4, r11, #(ITERATIONS_PER_LOOP - 1)
    rsb r4, r4, #ITERATIONS_PER_LOOP
    add pc, pc, r4, lsl #2
    nop
  .LwriteBarPixel:
    @ repeat the strh instruction ITERATIONS_PER_LOOP times
    .rept ITERATIONS_PER_LOOP
        strh r3, [r12], #SCREEN_WIDTH
    .endr
    subs r11, #ITERATIONS_PER_LOOP
    bge .LwriteBarPixel

.endif

  .LskipBar:

    add r7, r7, r6              @ lx += dx
    add r5, r5, r8              @ ly += dy
    add r10, r10, #1            @ i++
    cmp r10, #(SCREEN_WIDTH/2)
    blt .LnextColumn

    @ update z
    ldr r1, [sp, #local_saved_z]
    cmp r1, #256
    addhs r1, r1, #4
    cmp r1, #128
    addhs r1, r1, #2
    add r1, r1, #2

    cmp r1, #512
    blt .LnextZ

  .Lreturn:
    @ return
    add sp, sp, #(SCREEN_WIDTH/2+4)
    pop {r4-r12,lr}
    bx lr

bgColorFillValue:
    .fill 4, 1, BG_COLOR
yBufferFillValue:
    .fill 4, 1, SCREEN_HEIGHT

    .pool
