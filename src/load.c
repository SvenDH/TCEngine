#include "private_types.h"

#include "bstrlib.h"
#include "stb_ds.h"

#include "tinyimageformat_base.h"
#include "tinyimageformat_query.h"
#include "tinyimageformat_bits.h"
#include "tinyimageformat_apis.h"

#include <uv.h>

extern renderertype_t selected_api;

bool load_byte_code(resourcedir_t dir, const char* filename, binaryshaderstagedesc_t* out, uint64_t timestamp)
{
	char path[FS_MAX_PATH] = { 0 };
	fs_path_join(fs_get_resource_dir(dir), filename, path);
	stat_t stat;
	await(os_stat(&stat, path));
	if (timestamp > stat.last_altered) return false;
	fstream_t s;
	TC_ASSERT(fs_open_fstream(dir, filename, FILE_READ, NULL, &s) == true);
	out->bytecodesize = (uint32_t)fs_stream_size(&s);
	out->bytecode = tc_malloc(out->bytecodesize);
	fs_read_stream(&s, out->bytecode, out->bytecodesize);
	fs_close_stream(&s);
	return true;
}

#if defined(VULKAN)
void vk_compile_shader(
	renderer_t* r, shadertarget_t target, shaderstage_t stage, const char* filename, const char* outfile, uint32_t macrocount,
	shadermacro_t* macros, binaryshaderstagedesc_t* out, const char* entrypoint)
{
	TC_TEMP_INIT(temp);
	char filepath[FS_MAX_PATH] = { 0 };
	char outpath[FS_MAX_PATH] = { 0 };
	char log[FS_MAX_PATH] = { 0 };
	char logpath[FS_MAX_PATH] = { 0 };
	
	fs_path_join(fs_get_resource_dir(R_SHADER_SOURCES), filename, filepath);
	fs_path_join(fs_get_resource_dir(R_SHADER_BINARIES), outfile, outpath);

	const char** args = NULL;
	arrput(args, "-V");
	arrput(args, filepath);
	arrput(args, "-o");
	arrput(args, outpath);
	if (target >= shader_target_6_0) arrput(args, "--target-env vulkan1.1 ");
	if (target >= shader_target_6_3) arrput(args, "--target-env spirv1.4");
	if (entrypoint != NULL) {
		arrput(args, "-e");
		arrput(args, entrypoint);
	}
#ifdef _WINDOWS									// Add platform macro
	arrput(args, "-D WINDOWS");
#elif defined(__ANDROID__)
	arrput(args, "-D ANDROID");
#elif defined(__linux__)
	arrput(args, "-D LINUX");
#endif
	for (uint32_t i = 0; i < macrocount; i++) {	// Add user defined macros to the command line
		char* a = TC_CALLOC(&temp, snprintf(NULL, 0, "-D%s=%s", macros[i].definition, macros[i].value) + 1, sizeof(char));
		sprintf(a, "-D%s=%s", macros[i].definition, macros[i].value);
		arrput(args, (const char*)a);
	}
	const char* vksdkstr = os_get_env("VULKAN_SDK", &temp);
	char* glslang_path = NULL;
	if (vksdkstr) {
		glslang_path = (char*)TC_CALLOC(&temp, strlen(vksdkstr) + 64, sizeof(char));
		strcpy(glslang_path, vksdkstr);
		strcat(glslang_path, "/bin/glslangValidator");
	}
	else {
		glslang_path = (char*)TC_CALLOC(&temp, 64, sizeof(char));
		strcpy(glslang_path, "/usr/bin/glslangValidator");
	}
	fs_path_filename(outfile, log);
	strcat(log, "_compile.log");
	fs_path_join(fs_get_resource_dir(R_SHADER_BINARIES), log, logpath);
	if (await(os_system_run(glslang_path, args, arrlen(args), logpath)) == 0) {
		fstream_t s;
		TC_ASSERT(fs_open_fstream(R_SHADER_BINARIES, outfile, FILE_READ, NULL, &s) == true);
		out->bytecodesize = (uint32_t)fs_stream_size(&s);
		out->bytecode = tc_malloc(out->bytecodesize);
		fs_read_stream(&s, out->bytecode, out->bytecodesize);
		fs_close_stream(&s);
	}
	else {
		fstream_t s;
		if (!fs_open_fstream(R_SHADER_BINARIES, log, FILE_READ, NULL, &s))
			TRACE(LOG_ERROR, "Failed to compile shader %s", filepath);
		else {
			size_t size = fs_stream_size(&s);
			if (size) {
				char* errorlog = (char*)tc_malloc(size + 1);
				errorlog[size] = '\0';
				fs_read_stream(&s, errorlog, size);
				TRACE(LOG_ERROR, "Failed to compile shader %s with error\n%s", filepath, errorlog);
			}
			fs_close_stream(&s);
		}
	}
	arrfree(args);
	TC_TEMP_CLOSE(temp);
}
#endif

bool load_shader_stage_byte_code(renderer_t* r, shadertarget_t target, shaderstage_t stage, shaderstage_t allStages, const shaderstageloaddesc_t* desc, uint32_t macrocount, shadermacro_t* macros, binaryshaderstagedesc_t* out)
{
	const char* api = "";
	switch (selected_api) {
#if defined(VULKAN)
	case RENDERER_VULKAN:
		api = "VULKAN";
		break;
#endif
	default: 
		TC_ASSERT(false && "Renderer API name not defined");
		break;
	}
	int n = 0;
	bstring strings[3] = { 0 };
	char path[FS_MAX_PATH] = { 0 };
	char ext[FS_MAX_PATH] = { 0 };
	char filename[FS_MAX_PATH] = { 0 };
	unsigned char fnamebuf[FS_MAX_PATH] = { 0 };

	bstring filenamestr = &(struct tagbstring){FS_MAX_PATH, -1, fnamebuf};
	if (api[0] != '\0') filenamestr = bformat("%s/%s", api, desc->filename);
	else bcatcstr(filenamestr, desc->filename);
	TRACE(LOG_INFO, "Compiling shader in runtime: %s -> '%s' macrocount=%u", api, desc->filename, macrocount);
	fs_path_join(fs_get_resource_dir(R_SHADER_SOURCES), (const char*)filenamestr->data, path);
	
	stat_t stat;
	await(os_stat(&stat, path));
	uint64_t timestamp = stat.last_altered;
	
	bstring defines = bfromcstr("");
	balloc(defines, 64);
	for (uint32_t i = 0; i < macrocount; i++) {		// Apply user specified macros
		bcatcstr(defines, macros[i].definition);
		bcatcstr(defines, macros[i].value);
	}
	fs_path_ext(desc->filename, ext);
	fs_path_filename(desc->filename, filename);

	static const size_t seed = 0x31415926;
	size_t hash = stbds_hash_string(defines->data, seed);
	bstring bin = bformat("%s_%s_%zu_%s_%u", api, filename, hash, ext, target);
	bcatStatic(bin, ".bin");
	strings[n++] = filenamestr;
	strings[n++] = defines;
	strings[n++] = bin;
	if (!load_byte_code(R_SHADER_BINARIES, (const char*)bin->data, out, timestamp)) {			// Shader source is newer than binary
		switch (selected_api) {
#if defined(VULKAN)
			case RENDERER_VULKAN:
				vk_compile_shader(r, target, stage, (const char*)filenamestr->data, (const char*)bin->data, macrocount, macros, out, desc->entrypointname);
				break;
#endif
			default: break;
		}
		if (!out->bytecode) {
			TRACE(LOG_ERROR, "Error while generating bytecode for shader %s", desc->filename);
			for (int i = 0; i < n; i++) bdestroy(strings[i]);
			return false;
		}
	}
	for (int i = 0; i < n; i++) bdestroy(strings[i]);
	return true;
}

bool find_shader_stage(const char* ext, binaryshaderdesc_t* desc, binaryshaderstagedesc_t** out, shaderstage_t* stage)
{
	if (_stricmp(ext, "vert") == 0) {
		*out = &desc->vert;
		*stage = SHADER_STAGE_VERT;
	}
	else if (_stricmp(ext, "frag") == 0) {
		*out = &desc->frag;
		*stage = SHADER_STAGE_FRAG;
	}
	else if (_stricmp(ext, "tesc") == 0) {
		*out = &desc->hull;
		*stage = SHADER_STAGE_HULL;
	}
	else if (_stricmp(ext, "tese") == 0) {
		*out = &desc->domain;
		*stage = SHADER_STAGE_DOMN;
	}
	else if (_stricmp(ext, "geom") == 0) {
		*out = &desc->geom;
		*stage = SHADER_STAGE_GEOM;
	}
	else if (_stricmp(ext, "comp") == 0) {
		*out = &desc->comp;
		*stage = SHADER_STAGE_COMP;
	}
	else if (
		(_stricmp(ext, "rgen") == 0) || (_stricmp(ext, "rmiss") == 0) || (_stricmp(ext, "rchit") == 0) ||
		(_stricmp(ext, "rint") == 0) || (_stricmp(ext, "rahit") == 0) || (_stricmp(ext, "rcall") == 0)) {
		*out = &desc->comp;
		*stage = SHADER_STAGE_RAYTRACING;
	}
	else return false;
	return true;
}

void load_shader(renderer_t* r, const shaderloaddesc_t* desc, shader_t* shader)
{
	if ((uint32_t)desc->target > r->shadertarget) {
		TRACE(LOG_ERROR, 
			"Requested shader target (%u) is higher than the shader target that the renderer supports (%u). Shader wont be compiled",
			(uint32_t)desc->target, (uint32_t)r->shadertarget);
		return;
	}
	binaryshaderdesc_t bindesc = { 0 };
	shaderstage_t stages = SHADER_STAGE_NONE;
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; i++) {
		if (desc->stages[i].filename && desc->stages[i].filename[0] != 0) {
			shaderstage_t stage;
			binaryshaderstagedesc_t* stagedesc = NULL;
			char ext[FS_MAX_PATH] = { 0 };
			fs_path_ext(desc->stages[i].filename, ext);
			if (find_shader_stage(ext, &bindesc, &stagedesc, &stage)) stages |= stage;
		}
	}
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; i++) {
		if (desc->stages[i].filename && desc->stages[i].filename[0] != 0) {
			const char* filename = desc->stages[i].filename;
			shaderstage_t stage;
			binaryshaderstagedesc_t* stagedesc = NULL;
			char ext[FS_MAX_PATH] = { 0 };
			fs_path_ext(filename, ext);
			if (find_shader_stage(ext, &bindesc, &stagedesc, &stage)) {
				uint32_t macrocount = desc->stages[i].macrocount;
				shadermacro_t* macros = NULL;
				arrsetlen(macros, macrocount);
				for (uint32_t j = 0; j < desc->stages[i].macrocount; j++)
					macros[j] = desc->stages[i].macros[j];

				if (!load_shader_stage_byte_code(r, desc->target, stage, stages, &desc->stages[i], macrocount, macros, stagedesc)) {
					arrfree(macros);
					return;
				}
				arrfree(macros);
				bindesc.stages |= stage;
				if (desc->stages[i].entrypointname)
					stagedesc->entrypoint = desc->stages[i].entrypointname;
				else
					stagedesc->entrypoint = "main";
			}
		}
	}
	bindesc.constantcount = desc->constantcount;
	bindesc.constants = desc->constants;
	add_shaderbinary(r, &bindesc, shader);
}
