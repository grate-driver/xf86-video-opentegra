.exports
	[0] = "position";

.attributes
	[0] = "position";

.constants
	[0].z = 0.0;
	[0].w = 1.0;

.asm
EXEC(export[0]=vector)
	MOVv r63.xy**, a[0].xyzw
;

EXEC_END(export[0]=vector)
	MOVv r63.**zw, c[0].xyzw
;
