#include "private_types.h"

#include "bstrlib.h"
#include "stb_ds.h"

#include "tinyimageformat_base.h"
#include "tinyimageformat_query.h"
#include "tinyimageformat_bits.h"
#include "tinyimageformat_apis.h"

#include <uv.h>

extern renderertype_t selected_api;

#define FS_MAX_PATH 512


static const char* get_renderer_API_name()
{
	switch (selected_api) {
#if defined(VULKAN)
	case RENDERER_VULKAN: return "VULKAN";
#endif
	default: 
	TC_ASSERT(false && "Renderer API name not defined");
	return "";
	}
}

#if defined(VULKAN)
void vk_compile_shader(
	renderer_t* r, shadertarget_t target, shaderstage_t stage, const char* filename, const char* outfile, uint32_t macrocount,
	shadermacro_t* macros, binaryshaderstagedesc_t out, const char* entrypoint)
{
	bstring line = &(struct tagbstring){1,0,""};
	balloc(line, 512);
	char filePath[FS_MAX_PATH] = { 0 };
	fs_path_join(fs_resource_dir(RD_SHADER_SOURCES), filename, filePath);
	char outfilePath[FS_MAX_PATH] = { 0 };
	fs_path_join(fs_resource_dir(RD_SHADER_BINARIES), outfile, outfilePath);
	bformata(line, "-V \"%s\" -o \"%s\"", filePath, outfilePath);
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

	const char* vksdkstr = getenv("VULKAN_SDK");
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
	fs_path_join(fs_resource_dir(RD_SHADER_BINARIES), log, logpath);

	if (tc_os->system_run(glslang_path, args, 1, logpath) == 0) {
		stat_t stat;
		await(tc_os->stat(&stat, outfilePath));
		int size = stat.size;
		char* tmp = alloca(size);
		fd_t file = await(tc_os->open(outfilePath, FILE_READ));
		TC_ASSERT(file != TC_INVALID_FILE);
		int64_t res = await(tc_os->read(file, tmp, size, 0));
		TC_ASSERT(res >= 0);
		out->bytecodesize = size;
		out->pByteCode = allocShaderByteCode(pShaderByteCodeBuffer, 1, out->mByteCodeSize, filename);
		fsReadFromStream(&fh, out->pByteCode, out->mByteCodeSize);
		tc_os->close(&);
	}
	else {
		FileStream fh = {};
		// If for some reason the error file could not be created just log error msg
		if (!fsOpenStreamFromPath(RD_SHADER_BINARIES, log, FM_READ_BINARY, NULL, &fh)) {
			TRACE(LOG_ERROR, "Failed to compile shader %s", filePath);
		else {
			size_t size = fsGetStreamFileSize(&fh);
			if (size) {
				char* errorLog = (char*)tc_malloc(size + 1);
				errorLog[size] = 0;
				fsReadFromStream(&fh, errorLog, size);
				TRACE(LOG_ERROR, "Failed to compile shader %s with error\n%s", filePath, errorLog);
			}
			fsCloseStream(&fh);
		}
	}
	bdestroy(line);
	tc_free(glslang_path);
}
#endif

bool load_shader_stage_byte_code(renderer_t* r, shadertarget_t target, shaderstage_t stage, shaderstage_t allStages, const shaderstageloaddesc_t* desc, uint32_t macrocount, shadermacro_t* macros, binaryshaderstagedesc_t* out)
{
	const char* rendererApi = get_renderer_API_name();
	bstring* cleanupBstrings[4] = {NULL};
	int cleanupBstringsCount = 0;
	bstring code = bempty();
	cleanupBstrings[cleanupBstringsCount++] = &code;

	unsigned char filenameAPIBuf[FS_MAX_PATH];
	bstring filenameAPI = bemptyfromarr(filenameAPIBuf);
	cleanupBstrings[cleanupBstringsCount++] = &filenameAPI;

	if (rendererApi[0] != '\0')
		bformat(&filenameAPI, "%s/%s", rendererApi, desc->filename);
	else
		bcatcstr(&filenameAPI, desc->filename);

	// If there are no macros specified there's no change to the shader source, we can use the binary compiled by FSL offline.
	if (macrocount == 0) {
		loadByteCode(r, RD_SHADER_BINARIES, (const char*)filenameAPI.data, out, pShaderByteCodeBuffer, outMetadata);
		cleanup();
		return true;
	}
	TRACE(LOG_INFO, "Compiling shader in runtime: %s -> '%s' macrocount=%u", rendererApi, desc->filename, macrocount);

	time_t timeStamp = 0;
	FileStream sourceFileStream = {};
	bool sourceExists = fsOpenStreamFromPath(RD_SHADER_SOURCES, (const char*)filenameAPI.data, FM_READ_BINARY, NULL, &sourceFileStream);
	ASSERT(sourceExists && "No source shader present for file");

	if (!loadShaderSourceFile(r->pName, &sourceFileStream, (const char*)filenameAPI.data, &timeStamp, &code)) {
		fsCloseStream(&sourceFileStream);
		cleanup();
		return false;
	}

	bstring shaderDefines = bempty();
	cleanupBstrings[cleanupBstringsCount++] = &shaderDefines;

	balloc(&shaderDefines, 64);

	// Apply user specified macros
	for (uint32_t i = 0; i < macrocount; i++) {
		bcatcstr(&shaderDefines, macros[i].definition);
		bcatcstr(&shaderDefines, macros[i].value);
	}

	char extension[FS_MAX_PATH] = { 0 };
	fsGetPathExtension(desc->filename, extension);
	char filename[FS_MAX_PATH] = { 0 };
	fsGetPathFileName(desc->filename, filename);

	bstring binaryShaderComponent = bempty();
	cleanupBstrings[cleanupBstringsCount++] = &binaryShaderComponent;

	balloc(&binaryShaderComponent, 128);

	static const size_t seed = 0x31415926;
	size_t shaderDefinesHash = stbds_hash_bstring(&shaderDefines, seed);
	bformat(&binaryShaderComponent, "%s_%s_%zu_%s_%u", rendererApi, filename, shaderDefinesHash, extension, target);

	bcatliteral(&binaryShaderComponent, ".bin");

	// Shader source is newer than binary
	if (!check_for_byte_code(r, (const char*)binaryShaderComponent.data, timeStamp, out, pShaderByteCodeBuffer)) {
		switch (selected_api) {
#if defined(VULKAN)
			case RENDERER_API_VULKAN:
#if defined(__ANDROID__)
				vk_compile_shader(
					r, stage, (uint32_t)code.slen, (const char*)code.data, (const char*)binaryShaderComponent.data, macrocount, macros, out,
					pShaderByteCodeBuffer, desc->entrypointName);
				if (!save_byte_code((const char*)binaryShaderComponent.data, (char*)(out->bytecode), out->bytecodesize))
					TRACE(LOG_WARNING, "Failed to save byte code for file %s", desc->filename);
#else
				vk_compile_shader(
					r, target, stage, (const char*)filenameAPI.data, (const char*)binaryShaderComponent.data, macrocount, macros, out,
					pShaderByteCodeBuffer, desc->entrypointName);
#endif
				break;
#endif
			default: break;
		}
		if (!out->pByteCode) {
			TRACE(LOG_ERROR, "Error while generating bytecode for shader %s", desc->filename);
			fsCloseStream(&sourceFileStream);
			TC_ASSERT(false);
			for (int i = 0; i < cleanupBstringsCount; i++) bdestroy(cleanupBstrings[i]);
			return false;
		}
	}

	fsCloseStream(&sourceFileStream);
	for (int i = 0; i < cleanupBstringsCount; i++) bdestroy(cleanupBstrings[i]);
	return true;
}

bool find_shader_stage(const char* extension, binaryshaderdesc_t* desc, binaryshaderstagedesc_t** out, shaderstage_t* stage)
{
	if (_stricmp(extension, "vert") == 0) {
		*out = &desc->vert;
		*stage = SHADER_STAGE_VERT;
	}
	else if (_stricmp(extension, "frag") == 0) {
		*out = &desc->frag;
		*stage = SHADER_STAGE_FRAG;
	}
	else if (_stricmp(extension, "tesc") == 0) {
		*out = &desc->hull;
		*stage = SHADER_STAGE_HULL;
	}
	else if (_stricmp(extension, "tese") == 0) {
		*out = &desc->domain;
		*stage = SHADER_STAGE_DOMN;
	}
	else if (_stricmp(extension, "geom") == 0) {
		*out = &desc->geom;
		*stage = SHADER_STAGE_GEOM;
	}
	else if (_stricmp(extension, "comp") == 0) {
		*out = &desc->comp;
		*stage = SHADER_STAGE_COMP;
	}
	else if (
		(_stricmp(extension, "rgen") == 0) || (_stricmp(extension, "rmiss") == 0) || (_stricmp(extension, "rchit") == 0) ||
		(_stricmp(extension, "rint") == 0) || (_stricmp(extension, "rahit") == 0) || (_stricmp(extension, "rcall") == 0)) {
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
			binaryshaderstagedesc_t* stage = NULL;
			char ext[FS_MAX_PATH] = { 0 };
			fs_path_extension(desc->stages[i].filename, ext);
			if (find_shader_stage(ext, &bindesc, &stage, &stage)) stages |= stage;
		}
	}
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; i++) {
		if (desc->stages[i].filename && desc->stages[i].filename[0] != 0) {
			const char* filename = desc->stages[i].filename;
			shaderstage_t stage;
			binaryshaderstagedesc_t* stage = NULL;
			char ext[FS_MAX_PATH] = { 0 };
			fs_path_extension(filename, ext);
			if (find_shader_stage(ext, &bindesc, &stage, &stage)) {
				combinedFlags |= desc->stages[i].flags;
				uint32_t macrocount = desc->stages[i].macrocount;
				shadermacro_t* macros = NULL;
				arrsetlen(macros, macrocount);
				for (uint32_t j = 0; j < desc->stages[i].macrocount; j++)
					macros[j] = desc->stages[i].macros[j];

				if (!load_shader_stage_byte_code(r, desc->target, stage, stages, desc->stages[i], macrocount, macros, stage)) {
					arrfree(macros);
					return;
				}
				arrfree(macros);
				bindesc.stages |= stage;
				if (desc->stages[i].entrypointname)
					stage->entrypoint = desc->stages[i].entrypointname;
				else
					stage->entrypoint = "main";
			}
		}
	}
	bindesc.constantcount = desc->constantcount;
	bindesc.constants = desc->constants;
	add_shaderbinary(r, &bindesc, shader);
}
