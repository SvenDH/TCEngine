#include "private_types.h"


void addShader(renderer_t* r, const ShaderLoadDesc* pDesc, Shader** ppShader)
{
#ifndef DIRECT3D11
	if ((uint32_t)pDesc->mTarget > r->mShaderTarget)
	{
		LOGF(LogLevel::eERROR, 
			"Requested shader target (%u) is higher than the shader target that the renderer supports (%u). Shader wont be compiled",
			(uint32_t)pDesc->mTarget, (uint32_t)r->mShaderTarget
		);
		return;
	}
#endif

#ifdef TARGET_IOS

	uint32_t iosMacroCount = 0;
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
	{
		if (pDesc->mStages[i].pFileName && pDesc->mStages[i].pFileName[0] != 0)
			iosMacroCount += pDesc->mStages[i].mMacroCount;
	}

	// We can't compile binary shaders on IOS, generate shader directly from text
	if(iosMacroCount != 0)
	{
		// Binary shaders are not supported on iOS.
		ShaderDesc desc = {};
		bstring codes[SHADER_STAGE_COUNT];
		ShaderMacro* pMacros[SHADER_STAGE_COUNT] = {};
		for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
		{
			codes[i] = bempty();

			if (pDesc->mStages[i].pFileName && strlen(pDesc->mStages[i].pFileName))
			{
				ShaderStage stage;
				ShaderStageDesc* pStage = NULL;
				if (find_shader_stage(pDesc->mStages[i].pFileName, &desc, &pStage, &stage))
				{
					char metalFileName[FS_MAX_PATH] = { 0 };
					fsAppendPathExtension(pDesc->mStages[i].pFileName, "metal", metalFileName);
					FileStream fh = {};
					bool sourceExists = fsOpenStreamFromPath(RD_SHADER_SOURCES, metalFileName, FM_READ_BINARY, NULL, &fh);
					ASSERT(sourceExists);

					pStage->pName = pDesc->mStages[i].pFileName;
					time_t timestamp = 0;
					loadShaderSourceFile(r->pName, &fh, metalFileName, &timestamp, &codes[i]);
					pStage->pCode = (const char*)codes[i].data;
					if (pDesc->mStages[i].pEntryPointName)
						pStage->pEntryPoint = pDesc->mStages[i].pEntryPointName;
					else
						pStage->pEntryPoint = "stageMain";
					// Apply user specified shader macros
					pStage->mMacroCount = pDesc->mStages[i].mMacroCount;
					pMacros[i] = (ShaderMacro*)alloca(pStage->mMacroCount * sizeof(ShaderMacro));
					pStage->pMacros = pMacros[i];
					for (uint32_t j = 0; j < pDesc->mStages[i].mMacroCount; j++)
						pMacros[i][j] = pDesc->mStages[i].pMacros[j];
					fsCloseStream(&fh);
					desc.mStages |= stage;
				}
			}
		}

		desc.mConstantCount = pDesc->mConstantCount;
		desc.pConstants = pDesc->pConstants;

		addIosShader(r, &desc, ppShader);
		for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
		{
			bdestroy(&codes[i]);
		}
		return;
	}

#endif
	
	BinaryShaderDesc binaryDesc = {};
#if defined(METAL)
	char* pSources[SHADER_STAGE_COUNT] = {};
#endif

	ShaderStageLoadFlags combinedFlags = SHADER_STAGE_LOAD_FLAG_NONE;

	ShaderByteCodeBuffer shaderByteCodeBuffer = {};
#if !defined(PROSPERO)
	char bytecodeStack[ShaderByteCodeBuffer::kStackSize] = {};
	shaderByteCodeBuffer.pStackMemory = bytecodeStack;
#endif

#if defined(QUEST_VR)
	bool bIsMultivewVR = false;
#endif

	ShaderStage stages = SHADER_STAGE_NONE;
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
	{
		if (pDesc->mStages[i].pFileName && pDesc->mStages[i].pFileName[0] != 0)
		{
			ShaderStage            stage;
			BinaryShaderStageDesc* pStage = NULL;
			char                   ext[FS_MAX_PATH] = { 0 };
			fsGetPathExtension(pDesc->mStages[i].pFileName, ext);
			if (find_shader_stage(ext, &binaryDesc, &pStage, &stage))
				stages |= stage;
		}
	}
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
	{
		if (pDesc->mStages[i].pFileName && pDesc->mStages[i].pFileName[0] != 0)
		{
			const char* fileName = pDesc->mStages[i].pFileName;

			ShaderStage            stage;
			BinaryShaderStageDesc* pStage = NULL;
			char                   ext[FS_MAX_PATH] = { 0 };
			fsGetPathExtension(fileName, ext);
			if (find_shader_stage(ext, &binaryDesc, &pStage, &stage))
			{
				combinedFlags |= pDesc->mStages[i].mFlags;
				uint32_t macroCount = pDesc->mStages[i].mMacroCount;
				
				ShaderMacro* macros = NULL;
				arrsetlen(macros, macroCount);
				for (uint32_t macro = 0; macro < pDesc->mStages[i].mMacroCount; ++macro)
					macros[macro] = pDesc->mStages[i].pMacros[macro]; //-V595

				FSLMetadata metadata = {};
				if (!load_shader_stage_byte_code(
					r, pDesc->mTarget, stage, stages, pDesc->mStages[i], macroCount, macros, pStage, &shaderByteCodeBuffer, &metadata))
				{
					arrfree(macros);
					freeShaderByteCode(&shaderByteCodeBuffer, &binaryDesc);
					return;
				}
				arrfree(macros);

				binaryDesc.mStages |= stage;

#if defined(QUEST_VR)
				bIsMultivewVR |= metadata.mUseMultiView;

				// TODO: remove this assert after testing on Quest
				ASSERT(((pDesc->mStages[i].mFlags& SHADER_STAGE_LOAD_FLAG_ENABLE_VR_MULTIVIEW) != 0) == metadata.mUseMultiView);
#endif

#if defined(METAL)
				if (pDesc->mStages[i].pEntryPointName)
					pStage->pEntryPoint = pDesc->mStages[i].pEntryPointName;
				else
					pStage->pEntryPoint = "stageMain";

				char metalFileName[FS_MAX_PATH] = { 0 };
#if defined(TARGET_IOS)
				strcat(metalFileName, "IOS/");
#else
				strcat(metalFileName, "MACOS/");
#endif
				strcat(metalFileName, fileName);
				strcat(metalFileName, ".metal");

				FileStream fh = {};
				fsOpenStreamFromPath(RD_SHADER_SOURCES, metalFileName, FM_READ_BINARY, NULL, &fh);
				size_t metalFileSize = fsGetStreamFileSize(&fh);
				pSources[i] = (char*)tf_malloc(metalFileSize + 1);
				pStage->pSource = pSources[i];
				pStage->mSourceSize = (uint32_t)metalFileSize;
				fsReadFromStream(&fh, pSources[i], metalFileSize);
				pSources[i][metalFileSize] = 0;    // Ensure the shader text is null-terminated
				fsCloseStream(&fh);
#elif !defined(ORBIS) && !defined(PROSPERO)
				if (pDesc->mStages[i].pEntryPointName)
					pStage->pEntryPoint = pDesc->mStages[i].pEntryPointName;
				else
					pStage->pEntryPoint = "main";
#endif
			}
		}
	}

#if defined(PROSPERO)
	binaryDesc.mOwnByteCode = true;
#endif

	binaryDesc.mConstantCount = pDesc->mConstantCount;
	binaryDesc.pConstants = pDesc->pConstants;

	addShaderBinary(r, &binaryDesc, ppShader);
	freeShaderByteCode(&shaderByteCodeBuffer, &binaryDesc);

#if defined(QUEST_VR)
	if (ppShader)
	{
		(*ppShader)->mIsMultiviewVR = bIsMultivewVR;
		ASSERT(((combinedFlags & SHADER_STAGE_LOAD_FLAG_ENABLE_VR_MULTIVIEW) != 0) == bIsMultivewVR);
	}
#endif

#if defined(METAL)
	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
	{
		if (pSources[i])
		{
			tf_free(pSources[i]);
		}
	}
#endif
}