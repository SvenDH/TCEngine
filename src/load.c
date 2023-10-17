#include "private_types.h"

#include "bstrlib.h"
#include "stb_ds.h"

#include "tinyimageformat_base.h"
#include "tinyimageformat_query.h"
#include "tinyimageformat_bits.h"
#include "tinyimageformat_apis.h"

#include <uv.h>

extern renderertype_t selected_api;

void load_byte_code(resourcedir_t dir, const char* filename, binaryshaderstagedesc_t* out)
{
	char path[FS_MAX_PATH] = { 0 };
	fs_path_join(tc_get_resource_dir(dir), filename, path);
	fd_t fd = (fd_t)await(tc_os->open(path, FILE_READ));
	TC_ASSERT(fd != TC_INVALID_FILE);
	stat_t stat;
	await(tc_os->stat(&stat, path));
	out->bytecodesize = (uint32_t)stat.size;
	out->bytecode = tc_malloc(stat.size * sizeof(uint8_t));
	int64_t res = await(tc_os->read(fd, out->bytecode, stat.size, 0));
	TC_ASSERT(res >= 0);
	tc_os->close(fd);
}

#if defined(VULKAN)
void vk_compile_shader(
	renderer_t* r, shadertarget_t target, shaderstage_t stage, const char* filename, const char* outfile, uint32_t macrocount,
	shadermacro_t* macros, binaryshaderstagedesc_t* out, const char* entrypoint)
{
	TC_TEMP_INIT(temp);
	bstring line = &(struct tagbstring)bsStatic("");
	balloc(line, 512);
	char filePath[FS_MAX_PATH] = { 0 };
	fs_path_join(tc_get_resource_dir(RD_SHADER_SOURCES), filename, filePath);
	char outpath[FS_MAX_PATH] = { 0 };
	fs_path_join(tc_get_resource_dir(RD_SHADER_BINARIES), outfile, outpath);
	bformata(line, "-V \"%s\" -o \"%s\"", filePath, outpath);
	if (target >= shader_target_6_0) bcatStatic(line, " --target-env vulkan1.1 ");
	if (target >= shader_target_6_3) bcatStatic(line, " --target-env spirv1.4");
	if (entrypoint != NULL) bformata(line, " -e %s", entrypoint);
#ifdef _WINDOWS									// Add platform macro
	bcatStatic(line, " \"-D WINDOWS\"");
#elif defined(__ANDROID__)
	bcatStatic(line, " \"-D ANDROID\"");
#elif defined(__linux__)
	bcatStatic(line, " \"-D LINUX\"");
#endif
	for (uint32_t i = 0; i < macrocount; i++)	// Add user defined macros to the command line
		bformata(line, " \"-D%s=%s\"", macros[i].definition, macros[i].value);

	const char* vksdkstr = tc_os->get_env("VULKAN_SDK", &temp);
	char* glslang_path = NULL;
	if (vksdkstr) {
		glslang_path = (char*)tc_calloc(strlen(vksdkstr) + 64, sizeof(char));
		strcpy(glslang_path, vksdkstr);
		strcat(glslang_path, "/bin/glslang_path");
	}
	else {
		glslang_path = (char*)tc_calloc(64, sizeof(char));
		strcpy(glslang_path, "/usr/bin/glslang_path");
	}
	const char* args[1] = { (const char*)line->data };
	char log[FS_MAX_PATH] = { 0 };
	fs_path_filename(outfile, log);
	strcat(log, "_compile.log");
	char logpath[FS_MAX_PATH] = { 0 };
	fs_path_join(tc_get_resource_dir(RD_SHADER_BINARIES), log, logpath);
	if (tc_os->system_run(glslang_path, args, 1, logpath) == 0) {
		stat_t stat;
		await(tc_os->stat(&stat, outpath));
		out->bytecodesize = (uint32_t)stat.size;
		fd_t f = (fd_t)await(tc_os->open(outpath, FILE_READ));
		TC_ASSERT(f != TC_INVALID_FILE);
		out->bytecode = tc_malloc(stat.size * sizeof(uint8_t));
		int64_t res = await(tc_os->read(f, out->bytecode, stat.size, 0));
		TC_ASSERT(res >= 0);
		tc_os->close(f);
	}
	else {
		//Todo: log error from logpath
	}
	bdestroy(line);
	tc_free(glslang_path);
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
	bstring strings[4] = { 0 };
	bstring code = &(struct tagbstring)bsStatic("");

	unsigned char fnamebuf[FS_MAX_PATH] = { 0 };
	bstring filenamestr = &(struct tagbstring){FS_MAX_PATH, 0, fnamebuf};

	strings[n++] = code;
	strings[n++] = filenamestr;

	if (api[0] != '\0') filenamestr = bformat("%s/%s", api, desc->filename);
	else bcatcstr(filenamestr, desc->filename);
	
#define CLEANUP for (int i = 0; i < n; i++) bdestroy(strings[i]);

	// If there are no macros specified there's no change to the shader source, we can use the binary compiled by FSL offline.
	if (macrocount == 0) {
		load_byte_code(RD_SHADER_BINARIES, (const char*)filenamestr->data, out);
		CLEANUP
		return true;
	}
	TRACE(LOG_INFO, "Compiling shader in runtime: %s -> '%s' macrocount=%u", api, desc->filename, macrocount);

	char path[FS_MAX_PATH] = { 0 };
	fs_path_join(tc_get_resource_dir(RD_SHADER_SOURCES), (const char*)filenamestr->data, path);
	fd_t f = (fd_t)await(tc_os->open(path, FILE_READ));
	TC_ASSERT(f != TC_INVALID_FILE && "No source shader present for file");

	stat_t stat;
	await(tc_os->stat(&stat, path));
	uint64_t timestamp = stat.last_altered;
	char* mem = (char*)alloca(stat.size + 1);
	int64_t res = await(tc_os->read(f, mem, stat.size, 0));
	TC_ASSERT(res >= 0);
	mem[stat.size] = '\0';
	bcatblk(code, mem, (int)stat.size);
	
	bstring defines = &(struct tagbstring)bsStatic("");
	balloc(defines, 64);
	for (uint32_t i = 0; i < macrocount; i++) {		// Apply user specified macros
		bcatcstr(defines, macros[i].definition);
		bcatcstr(defines, macros[i].value);
	}
	char ext[FS_MAX_PATH] = { 0 };
	fs_path_ext(desc->filename, ext);
	char filename[FS_MAX_PATH] = { 0 };
	fs_path_filename(desc->filename, filename);

	static const size_t seed = 0x31415926;
	size_t hash = stbds_hash_string(defines->data, seed);
	bstring bin = bformat("%s_%s_%zu_%s_%u", api, filename, hash, ext, target);
	bcatStatic(bin, ".bin");
	balloc(bin, 128);

	strings[n++] = defines;
	strings[n++] = bin;
	
	if (timestamp > stat.last_altered) {			// Shader source is newer than binary
		load_byte_code(RD_SHADER_BINARIES, (const char*)bin->data, out);
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
			tc_os->close(f);
			TC_ASSERT(false);
			CLEANUP
			return false;
		}
	}
	tc_os->close(f);
	CLEANUP
	return true;
#undef CLEANUP
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
