.exports
	[0] = "position";
	[1] = "texcoords";

.attributes
	[0] = "position";
	[1] = "src_texcoords";
	[2] = "mask_texcoords";

.constants
	[0].z = 0.0;
	[0].w = 1.0;

	// [1] = "src_transform_mat[0].xyz src_width.w";
	// [2] = "src_transform_mat[1].xyz src_height.w";

	// [3] = "mask_transform_mat[0].xyz mask_width.w";
	// [4] = "mask_transform_mat[1].xyz mask_height.w";

.asm
EXEC(export[0]=vector)
	MOVv r63.xy**, a[0].xyzw
	MOVs r1.**zw, c[0].wwwz
;

EXEC(export[0]=vector)
	MOVv r63.**zw, c[0].xyzw
	MOVs r1.xy**, a[1].xyzw
;

EXEC
	DP3v r3.x***, r1.xyzw, c[1].xyzw
	RCPs r0.x***, c[1].wwww
;

EXEC
	DP3v r3.*y**, r1.xyzw, c[2].xyzw
	RCPs r0.*y**, c[2].wwww
;

EXEC
	MOVv r4.xy**, a[2].xyzw
	MOVs r4.**zw, c[0].wwwz
;

EXEC
	DP3v r3.**z*, r4.xyzw, c[3].xyzw
	RCPs r0.**z*, c[3].wwww
;

EXEC
	DP3v r3.***w, r4.xyzw, c[4].xyzw
	RCPs r0.***w, c[4].wwww
;

EXEC_END(export[1]=vector)
	MULv r63.xyzw, r3.xyzw, r0.xyzw
;
