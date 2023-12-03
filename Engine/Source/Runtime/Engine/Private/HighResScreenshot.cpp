// Copyright Epic Games, Inc. All Rights Reserved.
#include "HighResScreenshot.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"
#include "Materials/Material.h"
#include "Slate/SceneViewport.h"
#include "ImageWriteQueue.h"

static TAutoConsoleVariable<int32> CVarSaveEXRCompressionQuality(
	TEXT("r.SaveEXR.CompressionQuality"),
	1,
	TEXT("Defines how we save HDR screenshots in the EXR format.\n")
	TEXT(" 0: no compression\n")
	TEXT(" 1: default compression which can be slow (default)"),
	ECVF_RenderThreadSafe);

DEFINE_LOG_CATEGORY(LogHighResScreenshot);

FHighResScreenshotConfig& GetHighResScreenshotConfig()
{
	static FHighResScreenshotConfig Instance;
	return Instance;
}

const float FHighResScreenshotConfig::MinResolutionMultipler = 1.0f;
const float FHighResScreenshotConfig::MaxResolutionMultipler = 10.0f;

FHighResScreenshotConfig::FHighResScreenshotConfig()
	: ResolutionMultiplier(FHighResScreenshotConfig::MinResolutionMultipler)
	, ResolutionMultiplierScale(0.0f)
	, bMaskEnabled(false)
	, bDateTimeBasedNaming(false)
	, bDumpBufferVisualizationTargets(false)
{
	ChangeViewport(TWeakPtr<FSceneViewport>());
	SetHDRCapture(false);
	SetForce128BitRendering(false);
}

void FHighResScreenshotConfig::Init()
{
	ImageWriteQueue = &FModuleManager::LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue").GetWriteQueue();

#if WITH_EDITOR
	HighResScreenshotMaterial = LoadObject<UMaterial>(NULL, TEXT("/Engine/EngineMaterials/HighResScreenshot.HighResScreenshot"));
	HighResScreenshotMaskMaterial = LoadObject<UMaterial>(NULL, TEXT("/Engine/EngineMaterials/HighResScreenshotMask.HighResScreenshotMask"));
	HighResScreenshotCaptureRegionMaterial = LoadObject<UMaterial>(NULL, TEXT("/Engine/EngineMaterials/HighResScreenshotCaptureRegion.HighResScreenshotCaptureRegion"));

	if (HighResScreenshotMaterial)
	{
		HighResScreenshotMaterial->AddToRoot();
	}
	if (HighResScreenshotMaskMaterial)
	{
		HighResScreenshotMaskMaterial->AddToRoot();
	}
	if (HighResScreenshotCaptureRegionMaterial)
	{
		HighResScreenshotCaptureRegionMaterial->AddToRoot();
	}
#endif
}

void FHighResScreenshotConfig::PopulateImageTaskParams(FImageWriteTask& InOutTask)
{
	static const TConsoleVariableData<int32>* CVarDumpFramesAsHDR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.BufferVisualizationDumpFramesAsHDR"));

	const bool bCaptureHDREnabledInUI = bCaptureHDR && bDumpBufferVisualizationTargets;

	const bool bLocalCaptureHDR = bCaptureHDREnabledInUI || CVarDumpFramesAsHDR->GetValueOnAnyThread();

	InOutTask.Format = bLocalCaptureHDR ? EImageFormat::EXR : EImageFormat::PNG;

	InOutTask.CompressionQuality = (int32)EImageCompressionQuality::Default;
	if (bLocalCaptureHDR && CVarSaveEXRCompressionQuality.GetValueOnAnyThread() == 0)
	{
		InOutTask.CompressionQuality = (int32)EImageCompressionQuality::Uncompressed;
	}
}

void FHighResScreenshotConfig::ChangeViewport(TWeakPtr<FSceneViewport> InViewport)
{
	if (FSceneViewport* Viewport = TargetViewport.Pin().Get())
	{
		// Force an invalidate on the old viewport to make sure we clear away the capture region effect
		Viewport->Invalidate();
	}

	UnscaledCaptureRegion = FIntRect(0, 0, 0, 0);
	CaptureRegion = UnscaledCaptureRegion;
	bMaskEnabled = false;
	bDateTimeBasedNaming = false;
	bDumpBufferVisualizationTargets = false;
	ResolutionMultiplier = FHighResScreenshotConfig::MinResolutionMultipler;
	ResolutionMultiplierScale = 0.0f;
	TargetViewport = InViewport;
}

bool FHighResScreenshotConfig::ParseConsoleCommand(const FString& InCmd, FOutputDevice& Ar)
{
	GScreenshotResolutionX = 0;
	GScreenshotResolutionY = 0;
	ResolutionMultiplier = FHighResScreenshotConfig::MinResolutionMultipler;
	ResolutionMultiplierScale = 0.0f;

	if( GetHighResScreenShotInput(*InCmd, Ar, GScreenshotResolutionX, GScreenshotResolutionY, ResolutionMultiplier, CaptureRegion, bMaskEnabled, bDumpBufferVisualizationTargets, bCaptureHDR, FilenameOverride, bDateTimeBasedNaming) )
	{
		GScreenshotResolutionX *= ResolutionMultiplier;
		GScreenshotResolutionY *= ResolutionMultiplier;

		uint32 MaxTextureDimension = GetMax2DTextureDimension();

		// Check that we can actually create a destination texture of this size
		if ( GScreenshotResolutionX > MaxTextureDimension || GScreenshotResolutionY > MaxTextureDimension )
		{
			Ar.Logf(TEXT("Error: Screenshot size exceeds the maximum allowed texture size (%d x %d)"), GetMax2DTextureDimension(), GetMax2DTextureDimension());
			return false;
		}

		GIsHighResScreenshot = true;

		return true;
	}

	return false;
}

bool FHighResScreenshotConfig::MergeMaskIntoAlpha(TArray<FColor>& InBitmap)
{
	bool bWritten = false;

	TArray<FColor>* MaskArray = FScreenshotRequest::GetHighresScreenshotMaskColorArray();
	bool bMaskMatches = !bMaskEnabled || (MaskArray->Num() == InBitmap.Num());
	ensureMsgf(bMaskMatches, TEXT("Highres screenshot MaskArray doesn't match screenshot size.  Skipping Masking. MaskSize: %i, ScreenshotSize: %i"), MaskArray->Num(), InBitmap.Num());
	if (bMaskEnabled && bMaskMatches)
	{
		// If this is a high resolution screenshot and we are using the masking feature,
		// Get the results of the mask rendering pass and insert into the alpha channel of the screenshot.
		for (int32 i = 0; i < InBitmap.Num(); ++i)
		{
			InBitmap[i].A = (*MaskArray)[i].R;
		}
		bWritten = true;
	}
	else
	{
		// Ensure that all pixels' alpha is set to 255
		for (auto& Color : InBitmap)
		{
			Color.A = 255;
		}
	}

	return bWritten;
}

void FHighResScreenshotConfig::SetHDRCapture(bool bCaptureHDRIN)
{
	bCaptureHDR = bCaptureHDRIN;
}

void FHighResScreenshotConfig::SetForce128BitRendering(bool bForce)
{
	bForce128BitRendering = bForce;
}

bool FHighResScreenshotConfig::SetResolution(uint32 ResolutionX, uint32 ResolutionY, float ResolutionScale)
{
	if ((ResolutionX * ResolutionScale) > GetMax2DTextureDimension() || (ResolutionY * ResolutionScale) > GetMax2DTextureDimension())
	{
		// TODO LOG
		//Ar.Logf(TEXT("Error: Screenshot size exceeds the maximum allowed texture size (%d x %d)"), GetMax2DTextureDimension(), GetMax2DTextureDimension());
		return false;
	}

	UnscaledCaptureRegion = FIntRect(0, 0, 0, 0);
	CaptureRegion = UnscaledCaptureRegion;
	bMaskEnabled = false;

	GScreenshotResolutionX = (ResolutionX * ResolutionScale);
	GScreenshotResolutionY = (ResolutionY * ResolutionScale);
	GIsHighResScreenshot = true;

	return true;
}

void FHighResScreenshotConfig::SetFilename(FString Filename)
{
	FilenameOverride = Filename;
}

void FHighResScreenshotConfig::SetMaskEnabled(bool bShouldMaskBeEnabled)
{
	bMaskEnabled = bShouldMaskBeEnabled;
}
// ----------------------- BUFFER DUMP ---------------------------------------

FHighResStandaloneBufferDumpConfig& GetHighResStandaloneBufferDumpConfig()
{
	static FHighResStandaloneBufferDumpConfig Instance;
	return Instance;
}

const float FHighResStandaloneBufferDumpConfig::MinResolutionMultipler = 1.0f;
const float FHighResStandaloneBufferDumpConfig::MaxResolutionMultipler = 10.0f;

FHighResStandaloneBufferDumpConfig::FHighResStandaloneBufferDumpConfig()
	: ResolutionMultiplier(FHighResStandaloneBufferDumpConfig::MinResolutionMultipler)
	, ResolutionMultiplierScale(0.0f)
	//, bMaskEnabled(false)
	//, bDateTimeBasedNaming(true)
	, bStandaloneDumpBufferVisualizationTargets(false)
	, SelectedMaterialNames(FString(TEXT("")))
{
	ChangeViewport(TWeakPtr<FSceneViewport>());
	//SetHDRCapture(false);
	//SetForce128BitRendering(false);
}

void FHighResStandaloneBufferDumpConfig::Init()
{
	ImageWriteQueue = &FModuleManager::LoadModuleChecked<IImageWriteQueueModule>("ImageWriteQueue").GetWriteQueue();

#if WITH_EDITOR
	// same as for high res screenshot
	HighResStandaloneBufferDumpMaterial = LoadObject<UMaterial>(NULL, TEXT("/Engine/EngineMaterials/HighResScreenshot.HighResScreenshot"));
	HighResStandaloneBufferDumpCaptureRegionMaterial = LoadObject<UMaterial>(NULL, TEXT("/Engine/EngineMaterials/HighResScreenshotCaptureRegion.HighResScreenshotCaptureRegion"));

	if (HighResStandaloneBufferDumpMaterial)
	{
		HighResStandaloneBufferDumpMaterial->AddToRoot();
	}
	if (HighResStandaloneBufferDumpCaptureRegionMaterial)
	{
		HighResStandaloneBufferDumpCaptureRegionMaterial->AddToRoot();
	}
#endif
}

void FHighResStandaloneBufferDumpConfig::PopulateImageTaskParams(FImageWriteTask& InOutTask)
{
	InOutTask.Format = EImageFormat::PNG;
	InOutTask.CompressionQuality = (int32)EImageCompressionQuality::Default;
}

void FHighResStandaloneBufferDumpConfig::ChangeViewport(TWeakPtr<FSceneViewport> InViewport)
{
	if (FSceneViewport* Viewport = TargetViewport.Pin().Get())
	{
		// Force an invalidate on the old viewport to make sure we clear away the capture region effect
		Viewport->Invalidate();
	}

	UnscaledCaptureRegion = FIntRect(0, 0, 0, 0);
	CaptureRegion = UnscaledCaptureRegion;
	ResolutionMultiplier = FHighResStandaloneBufferDumpConfig::MinResolutionMultipler;
	ResolutionMultiplierScale = 0.0f;
	TargetViewport = InViewport;
}

bool FHighResStandaloneBufferDumpConfig::ParseConsoleCommand(const FString& InCmd, FOutputDevice& Ar)
{
	GVisualizationDumpResolutionX = 0;
	GVisualizationDumpResolutionY = 0;
	ResolutionMultiplier = FHighResStandaloneBufferDumpConfig::MinResolutionMultipler;
	ResolutionMultiplierScale = 0.0f;

	if (GetHighResStandaloneBufferDumpInput(*InCmd, Ar, GVisualizationDumpResolutionX, GVisualizationDumpResolutionY, ResolutionMultiplier, CaptureRegion, bStandaloneDumpBufferVisualizationTargets, FilenameOverride))
	{
		GVisualizationDumpResolutionX *= ResolutionMultiplier;
		GVisualizationDumpResolutionY *= ResolutionMultiplier;

		uint32 MaxTextureDimension = GetMax2DTextureDimension();

		// Check that we can actually create a destination texture of this size
		if (GVisualizationDumpResolutionX > MaxTextureDimension || GScreenshotResolutionY > MaxTextureDimension)
		{
			Ar.Logf(TEXT("Error: Buffer Dump size exceeds the maximum allowed texture size (%d x %d)"), GetMax2DTextureDimension(), GetMax2DTextureDimension());
			return false;
		}

		GIsHighResStandaloneBufferDump = true;

		return true;
	}

	return false;
}

bool FHighResStandaloneBufferDumpConfig::SetResolution(uint32 ResolutionX, uint32 ResolutionY, float ResolutionScale)
{
	if ((ResolutionX * ResolutionScale) > GetMax2DTextureDimension() || (ResolutionY * ResolutionScale) > GetMax2DTextureDimension())
	{
		// TODO LOG
		//Ar.Logf(TEXT("Error: Screenshot size exceeds the maximum allowed texture size (%d x %d)"), GetMax2DTextureDimension(), GetMax2DTextureDimension());
		return false;
	}

	UnscaledCaptureRegion = FIntRect(0, 0, 0, 0);
	CaptureRegion = UnscaledCaptureRegion;
	//bMaskEnabled = false;

	GVisualizationDumpResolutionX = (ResolutionX * ResolutionScale);
	GVisualizationDumpResolutionY = (ResolutionY * ResolutionScale);
	GIsHighResStandaloneBufferDump = true;

	return true;
}

void FHighResStandaloneBufferDumpConfig::SetFilename(FString Filename)
{
	FilenameOverride = Filename;
}

FString FHighResStandaloneBufferDumpConfig::GetSelectedMaterialNames() const
{
	return SelectedMaterialNames;
}

void FHighResStandaloneBufferDumpConfig::AddSelectedMaterialName(FString MaterialName)
{
	
	SelectedMaterialNames += MaterialName + ",";
}

void FHighResStandaloneBufferDumpConfig::ClearSelectedMaterials()
{
	SelectedMaterialNames.Reset();
}