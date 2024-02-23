#include "EdgegapSettingsDetails.h"
#include "EdgegapSettings.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Settings/ProjectPackagingSettings.h"

#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "AboutScreen.h"
#include "CreditsScreen.h"
#include "DesktopPlatformModule.h"
#include "ISourceControlModule.h"
#include "GameProjectGenerationModule.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "SourceCodeNavigation.h"
#include "SourceControlWindows.h"
#include "ISettingsModule.h"
#include "Interfaces/IProjectManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "PlatformInfo.h"
#include "EditorStyleSet.h"
#include "Settings/EditorExperimentalSettings.h"
#include "CookerSettings.h"
#include "UnrealEdMisc.h"
#include "FileHelpers.h"
#include "EditorAnalytics.h"
#include "LevelEditor.h"
#include "Interfaces/IProjectTargetPlatformEditorModule.h"
#include "InstalledPlatformInfo.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Commands/GenericCommands.h"
#include "Dialogs/SOutputLogDialog.h"
#include "IUATHelperModule.h"


#include "TargetReceipt.h"
#include "UnrealEdGlobals.h"
#include "Async/Async.h"
#include "Editor/UnrealEdEngine.h"

#include "Settings/EditorSettings.h"
#include "AnalyticsEventAttribute.h"
#include "Kismet2/DebuggerCommands.h"
#include "GameMapsSettings.h"
#include "DerivedDataCacheInterface.h"

#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "JsonObjectConverter.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Images/SImage.h"

#include "IDetailChildrenBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "GeneralProjectSettings.h"

#define LOCTEXT_NAMESPACE "EdgegapSettingsDetails"
#include "SExternalImageReference.h"


DEFINE_LOG_CATEGORY(EdgegapLog);

class SDeployStatusListItem
	: public SMultiColumnTableRow< TSharedPtr<struct FDeploymentStatusListItem> >
{
public:

	SLATE_BEGIN_ARGS(SDeployStatusListItem) { }
	SLATE_ARGUMENT(TSharedPtr<FDeploymentStatusListItem>, Item)
		SLATE_END_ARGS()

public:

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		Item = InArgs._Item;
		check(Item.IsValid());
		const auto args = FSuperRowType::FArguments();
		SMultiColumnTableRow< TSharedPtr<FDeploymentStatusListItem> >::Construct(args, InOwnerTableView);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == TEXT("Connection IP"))
		{
			return	SNew(SBox)
				.HeightOverride(20)
				.Padding(FMargin(3, 0))
				.VAlign(VAlign_Center)
				[
					SNew(SInlineEditableTextBlock)
					.Text(FText::FromString(Item->DeploymentIP))
					.Style(FCoreStyle::Get(), "InlineEditableTextBlockSmallStyle")
					.Cursor(EMouseCursor::TextEditBeam)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				];
		}
		else if (ColumnName == TEXT("Status"))
		{
			return	SNew(SBox)
				.HeightOverride(20)
				.Padding(FMargin(3, 0))
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item->DeploymentStatus))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				];
		}
		else if (ColumnName == TEXT("Control"))
		{
			bool DeploymentReady = Item->DeploymentReady;
			return SNew(SBox)
				.HeightOverride(20)
				.Padding(FMargin(3, 0))
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.Text(LOCTEXT("Stop", "Stop"))
				.IsEnabled(DeploymentReady)
				.OnClicked_Lambda([this]()
					{
						SetEnabled(false);

						// HTTP call to delete deployment
						FEdgegapSettingsDetails::StopDeploy(this->Item->RequestID, this->Item->API_Key);

						return(FReply::Handled());
					})
				];
		}

		return SNullWidget::NullWidget;
	}

	TSharedPtr< FDeploymentStatusListItem > Item;

};




FString FEdgegapSettingsDetails::_ImageName;
FString FEdgegapSettingsDetails::_RegistryURL;
FString FEdgegapSettingsDetails::_PrivateUsername;
FString FEdgegapSettingsDetails::_PrivateToken;
FString FEdgegapSettingsDetails::_API_key;
FString FEdgegapSettingsDetails::_AppName;
FString FEdgegapSettingsDetails::_VersionName;
TArray< TSharedPtr<FDeploymentStatusListItem > > FEdgegapSettingsDetails::DeployStatusOverrideListSource;
FEdgegapSettingsDetails* FEdgegapSettingsDetails::Singelton;

namespace{
	const TCHAR* GetUATCompilationFlags()
	{
		// We never want to compile editor targets when invoking UAT in this context.
		// If we are installed or don't have a compiler, we must assume we have a precompiled UAT.
		return TEXT("-nocompileeditor");
	}

	FString GetCookingOptionalParams()
	{
		FString OptionalParams;
		const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();

		if (PackagingSettings->bSkipEditorContent)
		{
			OptionalParams += TEXT(" -SkipCookingEditorContent");
		}

		if (FDerivedDataCacheInterface* DDC = GetDerivedDataCache())
		{
			OptionalParams += FString::Printf(TEXT(" -ddc=%s"), DDC->GetGraphName());
		}

		return OptionalParams;
	}

	void OnPackageCallaback(FString res, double num)
	{
		if (res != "Completed")
		{
			UE_LOG(EdgegapLog, Warning, TEXT("OnPackageCallaback: Could not package, message:%s"), *res);
			return;
		}

		auto Settings = FEdgegapSettingsDetails::GetInstance()->Settings;
		UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>(UProjectPackagingSettings::StaticClass()->GetDefaultObject());

		FString PluginDir = IPluginManager::Get().FindPlugin(FString("Edgegap"))->GetBaseDir();
		FString DockerFilePath = FPaths::Combine(PluginDir, FString("Dockerfile"));
		FString StartScriptPath = FPaths::Combine(PluginDir, FString("StartServer.sh"));
		FString ServerBuildPath = PackagingSettings->StagingDirectory.Path;
		ServerBuildPath = FPaths::Combine(ServerBuildPath, FString("LinuxServer"));
		
		AsyncTask(ENamedThreads::GameThread, [Settings, DockerFilePath, StartScriptPath, ServerBuildPath] {
			FEdgegapSettingsDetails::Containerize(DockerFilePath, StartScriptPath, ServerBuildPath, Settings->Registry, Settings->ImageRepository, Settings->Tag, Settings->PrivateRegistryUsername, Settings->PrivateRegistryToken); 
		});

	}

	void OnDockerLoginCallback(FString res, double num)
	{
		if (res != "Completed")
		{
			UE_LOG(EdgegapLog, Warning, TEXT("OnDockerLoginCallback: Could not login, message:%s"), *res);
			return;
		}
		auto Settings = FEdgegapSettingsDetails::GetInstance()->Settings;
		FString ImageName = FString::Printf(TEXT("%s/%s:%s"), *Settings->Registry, *Settings->ImageRepository, *Settings->Tag);

		AsyncTask(ENamedThreads::GameThread, [Settings, ImageName] {FEdgegapSettingsDetails::PushContainer(ImageName, Settings->Registry, Settings->PrivateRegistryUsername, Settings->PrivateRegistryToken, true); });
	}

	void OnContainerizeCallback(FString res, double num)
	{
		if (res != "Completed")
		{
			UE_LOG(EdgegapLog, Warning, TEXT("OnContainerizeCallback: Could not generate container, message:%s"), *res);
			return;
		}

		auto Settings = FEdgegapSettingsDetails::GetInstance()->Settings;
		FString ImageName = FString::Printf(TEXT("%s/%s:%s"), *Settings->Registry, *Settings->ImageRepository, *Settings->Tag);
		

		AsyncTask(ENamedThreads::GameThread, [Settings, ImageName] {FEdgegapSettingsDetails::PushContainer(ImageName, Settings->Registry, Settings->PrivateRegistryUsername, Settings->PrivateRegistryToken); });
	}	
	
	void OnPushContainerCallback(FString res, double num)
	{
		if (res != "Completed")
		{
			UE_LOG(EdgegapLog, Warning, TEXT("OnPushContainerCallback: Could not push container, message:%s"), *res);
			return;
		}
		auto Settings = FEdgegapSettingsDetails::GetInstance()->Settings;
		FEdgegapSettingsDetails::CreateVersion(Settings->ApplicationName.ToString(), Settings->VersionName, Settings->API_Key, Settings->Registry, Settings->ImageRepository, Settings->Tag, Settings->PrivateRegistryUsername, Settings->PrivateRegistryToken);

	}

	FString GetProjectPathForTurnkey()
	{
		if (FPaths::IsProjectFilePathSet())
		{
			return FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
		}
		if (FApp::HasProjectName())
		{
			FString ProjectPath = FPaths::ProjectDir() / FApp::GetProjectName() + TEXT(".uproject");
			if (FPaths::FileExists(ProjectPath))
			{
				return ProjectPath;
			}
			ProjectPath = FPaths::RootDir() / FApp::GetProjectName() / FApp::GetProjectName() + TEXT(".uproject");
			if (FPaths::FileExists(ProjectPath))
			{
				return ProjectPath;
			}
		}
		return FString();
	}
	
	bool CheckSupportedPlatforms(FName IniPlatformName)
	{
	#if WITH_EDITOR
			return FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").ShowUnsupportedTargetWarning(IniPlatformName);
	#endif

		return true;
	}

	UProjectPackagingSettings* GetPackagingSettingsForPlatform(FName IniPlatformName)
	{
		FString PlatformString = IniPlatformName.ToString();
		UProjectPackagingSettings* PackagingSettings = nullptr;
		for (TObjectIterator<UProjectPackagingSettings> Itr; Itr; ++Itr)
		{
			if (Itr->GetConfigPlatform() == PlatformString)
			{
				PackagingSettings = *Itr;
				break;
			}
		}
		if (PackagingSettings == nullptr)
		{
			PackagingSettings = NewObject<UProjectPackagingSettings>(GetTransientPackage());
			// Prevent object from being GCed.
			PackagingSettings->AddToRoot();
			// make sure any changes to DefaultGame are updated in this class
			PackagingSettings->LoadSettingsForPlatform(PlatformString);
		}

		return PackagingSettings;
	}

	FString GetUATOptions()
	{
	#if WITH_EDITOR
			FString Options;
			Options += FString::Printf(TEXT(" -unrealexe=\"%s\""), *FUnrealEdMisc::Get().GetExecutableForCommandlets());

			int32 NumCookers = GetDefault<UEditorExperimentalSettings>()->MultiProcessCooking;
			if (NumCookers > 0)
			{
				Options += FString::Printf(TEXT(" -NumCookersToSpawn=%d"), NumCookers);
			}
			return Options;
	#else
			return TEXT("");
	#endif
	}

	bool ShouldBuildProject(UProjectPackagingSettings* PackagingSettings, const ITargetPlatform* TargetPlatform)
	{
		const UProjectPackagingSettings::FConfigurationInfo& ConfigurationInfo = UProjectPackagingSettings::ConfigurationInfo[(int)PackagingSettings->BuildConfiguration];

		// Get the target to build
		const FTargetInfo* Target = PackagingSettings->GetBuildTargetInfo();

		// Only build if the user elects to do so
		bool bBuild = false;
		if (PackagingSettings->Build == EProjectPackagingBuild::Always)
		{
			bBuild = true;
		}
		else if (PackagingSettings->Build == EProjectPackagingBuild::Never)
		{
			bBuild = false;
		}
		else if (PackagingSettings->Build == EProjectPackagingBuild::IfProjectHasCode)
		{
			bBuild = true;
			if (FApp::GetEngineIsPromotedBuild())
			{
				FString BaseDir;

				// Get the target name
				FString TargetName;
				if (Target == nullptr)
				{
					TargetName = TEXT("UnrealGame");
				}
				else
				{
					TargetName = Target->Name;
				}

				// Get the directory containing the receipt for this target, depending on whether the project needs to be built or not
				FString ProjectDir = FPaths::GetPath(FPaths::GetProjectFilePath());
				if (Target != nullptr && FPaths::IsUnderDirectory(Target->Path, ProjectDir))
				{
					UE_LOG(EdgegapLog, Log, TEXT("Selected target: %s"), *Target->Name);
					BaseDir = ProjectDir;
				}
				else
				{
					FText Reason;

					FGameProjectGenerationModule& GameProjectModule = FModuleManager::LoadModuleChecked<FGameProjectGenerationModule>(TEXT("GameProjectGeneration"));

					if (TargetPlatform->RequiresTempTarget(GameProjectModule.Get().ProjectHasCodeFiles(), ConfigurationInfo.Configuration, false, Reason))
					{
						UE_LOG(EdgegapLog, Log, TEXT("Project requires temp target (%s)"), *Reason.ToString());
						BaseDir = ProjectDir;
					}
					else
					{
						UE_LOG(EdgegapLog, Log, TEXT("Project does not require temp target"));
						BaseDir = FPaths::EngineDir();
					}
				}

				// Check if the receipt is for a matching promoted target
				FString UBTPlatformName = TargetPlatform->GetTargetPlatformInfo().DataDrivenPlatformInfo->UBTPlatformString;

				
				bool HasPromotedTarget(const TCHAR * BaseDir, const TCHAR * TargetName, const TCHAR * Platform, EBuildConfiguration Configuration, const TCHAR * Architecture);
				if (HasPromotedTarget(*BaseDir, *TargetName, *UBTPlatformName, ConfigurationInfo.Configuration, nullptr))
				{
					bBuild = false;
				}
			}
		}
		else if (PackagingSettings->Build == EProjectPackagingBuild::IfEditorWasBuiltLocally)
		{
			bBuild = !FApp::GetEngineIsPromotedBuild();
		}

		return bBuild;
	}

	bool DoesProjectHaveCode()
	{
		FGameProjectGenerationModule& GameProjectModule = FModuleManager::LoadModuleChecked<FGameProjectGenerationModule>(TEXT("GameProjectGeneration"));
		return GameProjectModule.Get().ProjectHasCodeFiles();
	}

	bool HasPromotedTarget(const TCHAR* BaseDir, const TCHAR* TargetName, const TCHAR* Platform, EBuildConfiguration Configuration, const TCHAR* Architecture)
	{
		// Get the path to the receipt, and check it exists
		const FString ReceiptPath = FTargetReceipt::GetDefaultPath(BaseDir, TargetName, Platform, Configuration, Architecture);
		if (!FPaths::FileExists(ReceiptPath))
		{
			UE_LOG(EdgegapLog, Log, TEXT("Unable to use promoted target - %s does not exist."), *ReceiptPath);
			return false;
		}

		// Read the receipt for this target
		FTargetReceipt Receipt;
		if (!Receipt.Read(ReceiptPath))
		{
			UE_LOG(EdgegapLog, Log, TEXT("Unable to use promoted target - cannot read %s"), *ReceiptPath);
			return false;
		}

		// Check the receipt is for a promoted build
		if (!Receipt.Version.IsPromotedBuild)
		{
			UE_LOG(EdgegapLog, Log, TEXT("Unable to use promoted target - receipt %s is not for a promoted target"), *ReceiptPath);
			return false;
		}

		// Make sure it matches the current build info
		FEngineVersion ReceiptVersion = Receipt.Version.GetEngineVersion();
		FEngineVersion CurrentVersion = FEngineVersion::Current();
		if (!ReceiptVersion.ExactMatch(CurrentVersion))
		{
			UE_LOG(EdgegapLog, Log, TEXT("Unable to use promoted target - receipt version (%s) is not exact match with current engine version (%s)"), *ReceiptVersion.ToString(), *CurrentVersion.ToString());
			return false;
		}

		// Print the matching target info
		UE_LOG(EdgegapLog, Log, TEXT("Found promoted target with matching version at %s"), *ReceiptPath);
		return true;
	}

};



TSharedRef<IDetailCustomization> FEdgegapSettingsDetails::MakeInstance()
{
	auto obj = MakeShareable(new FEdgegapSettingsDetails);
	Singelton = obj.Object;
	return obj;
}

void FEdgegapSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	check(ObjectsBeingCustomized.Num() == 1);
	Settings = Cast<UEdgegapSettings>(ObjectsBeingCustomized[0].Get());

	// Adjust order of categories
	IDetailCategoryBuilder& DocumentationCategoryBuilder = DetailBuilder.EditCategory("Documentation");
	IDetailCategoryBuilder& APICategoryBuilder = DetailBuilder.EditCategory("API");
	IDetailCategoryBuilder& DetailCategoryBuilder = DetailBuilder.EditCategory("Application Info");
	IDetailCategoryBuilder& VersionCategoryBuilder = DetailBuilder.EditCategory("Version");
	IDetailCategoryBuilder& ContainerCategoryBuilder = DetailBuilder.EditCategory("Container");
	IDetailCategoryBuilder& DeploymentStatusCategoryBuilder = DetailBuilder.EditCategory("Deployments");


	Add_Documentation_UI(DetailBuilder);
	Add_API_UI(DetailBuilder);
	AddAppInfoUI(DetailBuilder);
	AddContainerUI(DetailBuilder);
	AddDeploymentStatusTableUI(DetailBuilder);
}

void FEdgegapSettingsDetails::Add_Documentation_UI(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& DocumentationCategoryBuilder = DetailBuilder.EditCategory("Documentation");
		DocumentationCategoryBuilder.AddCustomRow(LOCTEXT("Documentation", "Documentation"))
		.WholeRowContent()
		.HAlign(EHorizontalAlignment::HAlign_Center)
		[
			SNew(SHorizontalBox)+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).HAlign(EHorizontalAlignment::HAlign_Left)
			[
				SAssignNew(StartUploadyButton, SButton).Text(LOCTEXT("Documentation", "Documentation")).OnClicked_Lambda(
					[this]()
					{
						FPlatformProcess::LaunchURL(TEXT("https://docs.edgegap.com/docs/category/unreal"), NULL, NULL);
						return(FReply::Handled());
					}
				)
			]
		];
}

void FEdgegapSettingsDetails::Add_API_UI(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& AppInfoCategory = DetailBuilder.EditCategory("API");

	AppInfoCategory.AddCustomRow(LOCTEXT("API", "API"))
	.WholeRowContent()
	.HAlign(EHorizontalAlignment::HAlign_Center)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(EHorizontalAlignment::HAlign_Center)
		.Padding(0,10,50,25)
		[
			SNew(SImage)
			.Image(this, &FEdgegapSettingsDetails::HandleImage)
		]
	];
}

void FEdgegapSettingsDetails::AddAppInfoUI(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& AppInfoCategory = DetailBuilder.EditCategory("Application Info");


	AppInfoCategory.AddCustomRow(LOCTEXT("Application Info", "Application Info"))
		.WholeRowContent()
		.HAlign(EHorizontalAlignment::HAlign_Left)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(EHorizontalAlignment::HAlign_Left)
		[
			SAssignNew(StartUploadyButton, SButton)
			.Text(LOCTEXT("Create Application", "Create Application"))
			.OnClicked_Lambda([this]()
			{
				CreateApp(Settings->ApplicationName.ToString(), Settings->ImagePath.FilePath, Settings->API_Key);
				return(FReply::Handled());
			})
		]
		];
}

void FEdgegapSettingsDetails::AddContainerUI(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& AppInfoCategory = DetailBuilder.EditCategory("Container");

	AppInfoCategory.AddCustomRow(LOCTEXT("Container", "Container"))
		.WholeRowContent()
		.HAlign(EHorizontalAlignment::HAlign_Left)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(EHorizontalAlignment::HAlign_Left)
		.Padding(0, 0, 10, 0)
		[
			SAssignNew(StartUploadyButton, SButton)
			.Text(LOCTEXT("Build&Push", "Build & Push"))
		.OnClicked_Lambda([this]()
			{
				const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();
				UProjectPackagingSettings* PackagingSettings = GetMutableDefault<UProjectPackagingSettings>();

				PackageProject("linux");
				return(FReply::Handled());
			})
		]

		];
}

void FEdgegapSettingsDetails::AddDeploymentStatusTableUI(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& DepStatusCategory = DetailBuilder.EditCategory("Deployments");

	DepStatusCategory.AddCustomRow(LOCTEXT("DeploymentStatus", "Deployments"))
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(5)
			.AutoWidth()
			[
				SAssignNew(DeploymentStatuRefreshButton, SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.OnClicked_Lambda([this]()
					{
						DeploymentStatuRefreshButton->SetEnabled(false);
						GetDeploymentsInfo(Settings->API_Key);
						return(FReply::Handled());
					})
			]
			+ SHorizontalBox::Slot()
				.Padding(0, 0, 10, 0)
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(EHorizontalAlignment::HAlign_Left)
				[
					SAssignNew(StartUploadyButton, SButton)
					.Text(LOCTEXT("Deploy", "Deploy Application"))
				.OnClicked_Lambda([this]()
					{
						DeployApp(Settings->ApplicationName.ToString(), Settings->VersionName, Settings->API_Key);
						return(FReply::Handled());
					})
				]
		];

	DepStatusCategory.AddCustomRow(LOCTEXT("DeploymentStatus", "Current Deployments"))
		[
			SAssignNew(DeploymentStatusListItemListView, SDeploymentStatusListItemListView)
			.ItemHeight(20.0f)
			.ListItemsSource(&DeployStatusOverrideListSource)
			.OnGenerateRow(this, &FEdgegapSettingsDetails::HandleGenerateDeployStatusWidget)
			.SelectionMode(ESelectionMode::None)
			.HeaderRow(
				SNew(SHeaderRow)
				// 
				+ SHeaderRow::Column("Connection IP")
				.HAlignCell(HAlign_Left)
				.FillWidth(1)
				.HeaderContentPadding(FMargin(0, 3))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DeploymentStatus_ConnectionIP", "Connection IP"))
					.Visibility(EVisibility::Visible)
					.IsEnabled(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				]
		// 
		+ SHeaderRow::Column("Status")
			.HAlignCell(HAlign_Left)
			.FillWidth(1)
			.HeaderContentPadding(FMargin(0, 3))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DeploymentStatus_Status", "Status"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		// 
		+ SHeaderRow::Column("Control")
			.HAlignCell(HAlign_Left)
			.FillWidth(1)
			.HeaderContentPadding(FMargin(0, 3))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DeploymentStatus_Control", "Control"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		)
	];
}

void FEdgegapSettingsDetails::SaveAll()
{
	const bool bPromptUserToSave = false;
	const bool bSaveMapPackages = true;
	const bool bSaveContentPackages = true;
	const bool bFastSave = false;
	const bool bNotifyNoPackagesSaved = false;
	const bool bCanBeDeclined = false;
	FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave, bNotifyNoPackagesSaved, bCanBeDeclined);
}

void FEdgegapSettingsDetails::PackageProject(const FName IniPlatformName)
{
	// get a in-memory defaults which will have the user-settings, like the per-platform config/target platform stuff
	UProjectPackagingSettings* AllPlatformPackagingSettings = GetMutableDefault<UProjectPackagingSettings>();

	// installed builds only support standard Game type builds (not Client, Server, etc) so instead of looking up a setting that the user can't set, 
	// always use the base PlatformInfo for Game builds, which will be named the same as the platform itself
	const PlatformInfo::FTargetPlatformInfo* PlatformInfo = nullptr;
	if (FApp::IsInstalled())
	{
		PlatformInfo = PlatformInfo::FindPlatformInfo(IniPlatformName);
	}
	else
	{
		PlatformInfo = PlatformInfo::FindPlatformInfo(AllPlatformPackagingSettings->GetTargetFlavorForPlatform(IniPlatformName));
	}
	// this is unexpected to be able to happen, but it could if there was a bad value saved in the UProjectPackagingSettings - if this trips, we should handle errors
	check(PlatformInfo != nullptr);

	const FString UBTPlatformString = PlatformInfo->DataDrivenPlatformInfo->UBTPlatformString;
	const FString ProjectPath = GetProjectPathForTurnkey();

	// check that we can proceed
	{
		if (FInstalledPlatformInfo::Get().IsPlatformMissingRequiredFile(UBTPlatformString))
		{
			if (!FInstalledPlatformInfo::OpenInstallerOptions())
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MissingPlatformFilesCook", "Missing required files to cook for this platform."));
			}
			return;
		}

		if (!CheckSupportedPlatforms(IniPlatformName))
		{
			return;
		}


	}

	// force a save of dirty packages before proceeding to run UAT
	// this may delete UProjectPackagingSettings , don't hold it across this call
	FEdgegapSettingsDetails::SaveAll();

	// get all the helper objects
	UProjectPackagingSettings* PackagingSettings = GetPackagingSettingsForPlatform(IniPlatformName);

	// basic BuildCookRun params we always want
	FString BuildCookRunParams = FString::Printf(TEXT("-nop4 -utf8output %s -cook "), GetUATCompilationFlags());


	// set locations to engine and project
	if (!ProjectPath.IsEmpty())
	{
		BuildCookRunParams += FString::Printf(TEXT(" -project=\"%s\""), *ProjectPath);
	}

	bool bIsProjectBuildTarget = false;
	const FTargetInfo* BuildTargetInfo = AllPlatformPackagingSettings->GetBuildTargetInfoForPlatform(IniPlatformName, bIsProjectBuildTarget);

	// Only add the -Target=... argument for code projects. Content projects will return UnrealGame/UnrealClient/UnrealServer here, but
	// may need a temporary target generated to enable/disable plugins. Specifying -Target in these cases will cause packaging to fail,
	// since it'll have a different name.
	if (BuildTargetInfo && bIsProjectBuildTarget)
	{
		BuildCookRunParams += FString::Printf(TEXT(" -target=%s"), *FString::Printf(TEXT("%sServer"), FApp::GetProjectName()));
	}

	// let the editor add options (-ue4exe in particular)
	{
		BuildCookRunParams += FString::Printf(TEXT(" %s"), *GetUATOptions());
	}

	// set the platform we are preparing content for
	{
		BuildCookRunParams += FString::Printf(TEXT(" -platform=%s"), *UBTPlatformString);
	}

	// Append any extra UAT flags specified for this platform flavor
	if (!PlatformInfo->UATCommandLine.IsEmpty())
	{
		BuildCookRunParams += FString::Printf(TEXT(" %s"), *PlatformInfo->UATCommandLine);
	}

	// optional settings
	if (PackagingSettings->bSkipEditorContent)
	{
		BuildCookRunParams += TEXT(" -SkipCookingEditorContent");
	}
	if (FDerivedDataCacheInterface* DDC = GetDerivedDataCache())
	{
		BuildCookRunParams += FString::Printf(TEXT(" -ddc=%s"), DDC->GetGraphName());
	}
	if (FApp::IsEngineInstalled())
	{
		BuildCookRunParams += TEXT(" -installed");
	}

	// per mode settings
	FText ContentPrepDescription;
	FText ContentPrepTaskName;
	const FSlateBrush* ContentPrepIcon = nullptr;
	if (true)
	{
		ContentPrepDescription = LOCTEXT("PackagingProjectTaskName", "Packaging project");
		ContentPrepTaskName = LOCTEXT("PackagingTaskName", "Packaging");
		ContentPrepIcon = FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject"));

		// let the user pick a target directory
		if (AllPlatformPackagingSettings->StagingDirectory.Path.IsEmpty())
		{
			AllPlatformPackagingSettings->StagingDirectory.Path = FPaths::ProjectDir();
		}

		FString OutFolderName;

		if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr), LOCTEXT("PackageDirectoryDialogTitle", "Package project...").ToString(), AllPlatformPackagingSettings->StagingDirectory.Path, OutFolderName))
		{
			return;
		}

		AllPlatformPackagingSettings->StagingDirectory.Path = OutFolderName;
		AllPlatformPackagingSettings->SaveConfig();


		BuildCookRunParams += TEXT(" -stage -archive -package");

		const ITargetPlatform* TargetPlatform = GetTargetPlatformManager()->FindTargetPlatform(PlatformInfo->Name);
		if (ShouldBuildProject(PackagingSettings, TargetPlatform))
		{
			BuildCookRunParams += TEXT(" -build");
		}

		if (PackagingSettings->FullRebuild)
		{
			BuildCookRunParams += TEXT(" -clean");
		}

		if (PackagingSettings->bCompressed)
		{
			BuildCookRunParams += TEXT(" -compressed");
		}

		if (PackagingSettings->bUseIoStore)
		{
			BuildCookRunParams += TEXT(" -iostore");

			// Pak file(s) must be used when using container file(s)
			PackagingSettings->UsePakFile = true;
		}

		if (PackagingSettings->UsePakFile)
		{
			BuildCookRunParams += TEXT(" -pak");
		}

		if (PackagingSettings->IncludePrerequisites)
		{
			BuildCookRunParams += TEXT(" -prereqs");
		}

		if (!PackagingSettings->ApplocalPrerequisitesDirectory.Path.IsEmpty())
		{
			BuildCookRunParams += FString::Printf(TEXT(" -applocaldirectory=\"%s\""), *(PackagingSettings->ApplocalPrerequisitesDirectory.Path));
		}
		else if (PackagingSettings->IncludeAppLocalPrerequisites)
		{
			BuildCookRunParams += TEXT(" -applocaldirectory=\"$(EngineDir)/Binaries/ThirdParty/AppLocalDependencies\"");
		}

		BuildCookRunParams += FString::Printf(TEXT(" -archivedirectory=\"%s\""), *AllPlatformPackagingSettings->StagingDirectory.Path);

		if (PackagingSettings->ForDistribution)
		{
			BuildCookRunParams += TEXT(" -distribution");
		}

		if (PackagingSettings->bGenerateChunks)
		{
			BuildCookRunParams += TEXT(" -manifests");
		}

		// Whether to include the crash reporter.
		if (PackagingSettings->IncludeCrashReporter && PlatformInfo->DataDrivenPlatformInfo->bCanUseCrashReporter)
		{
			BuildCookRunParams += TEXT(" -CrashReporter");
		}

		if (PackagingSettings->bBuildHttpChunkInstallData)
		{
			BuildCookRunParams += FString::Printf(TEXT(" -manifests -createchunkinstall -chunkinstalldirectory=\"%s\" -chunkinstallversion=%s"), *(PackagingSettings->HttpChunkInstallDataDirectory.Path), *(PackagingSettings->HttpChunkInstallDataVersion));
		}

		EProjectPackagingBuildConfigurations BuildConfig = AllPlatformPackagingSettings->GetBuildConfigurationForPlatform(IniPlatformName);
		BuildConfig = EProjectPackagingBuildConfigurations::PPBC_Shipping;
		const UProjectPackagingSettings::FConfigurationInfo& ConfigurationInfo = UProjectPackagingSettings::ConfigurationInfo[(int)BuildConfig];

		BuildCookRunParams += FString::Printf(TEXT(" -server -noclient -serverconfig=%s"), LexToString(ConfigurationInfo.Configuration));

		if (ConfigurationInfo.Configuration == EBuildConfiguration::Shipping && !PackagingSettings->IncludeDebugFiles)
		{
			BuildCookRunParams += TEXT(" -nodebuginfo");
		}
	}

	FString TurnkeyParams = FString::Printf(TEXT("-command=VerifySdk -platform=%s -UpdateIfNeeded"), *UBTPlatformString);
	if (!ProjectPath.IsEmpty())
	{
		TurnkeyParams.Appendf(TEXT(" -project=\"%s\""), *ProjectPath);
	}

	FString CommandLine;
	if (!ProjectPath.IsEmpty())
	{
		CommandLine.Appendf(TEXT("-ScriptsForProject=\"%s\" "), *ProjectPath);
	}
	CommandLine.Appendf(TEXT("Turnkey %s BuildCookRun %s"), *TurnkeyParams, *BuildCookRunParams);

	IUATHelperModule::Get().CreateUatTask(CommandLine, PlatformInfo->DisplayName, ContentPrepDescription, ContentPrepTaskName, ContentPrepIcon, &OnPackageCallaback);
}

void FEdgegapSettingsDetails::AddMessageLog(const FText& Text, const FText& Detail, const FString& TutorialLink, const FString& DocumentationLink)
{
	TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error);
	Message->AddToken(FTextToken::Create(Text));
	Message->AddToken(FTextToken::Create(Detail));
	if (!TutorialLink.IsEmpty())
	{
		Message->AddToken(FTutorialToken::Create(TutorialLink));
	}
	if (!DocumentationLink.IsEmpty())
	{
		Message->AddToken(FDocumentationToken::Create(DocumentationLink));
	}
	FMessageLog MessageLog("PackagingResults");
	MessageLog.AddMessage(Message);
	MessageLog.Open();
}

void FEdgegapSettingsDetails::Containerize(FString DockerFilePath, FString StartScriptPath, FString ServerBuildPath, FString RegistryURL, FString ImageRepository,  FString Tag, FString PrivateUsername, FString PrivateToken)
{
	const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();

	_PrivateUsername = PrivateUsername;
	_PrivateToken= PrivateToken;
	_RegistryURL = RegistryURL;
	_ImageName = FString::Printf(TEXT("%s/%s:%s"), *RegistryURL, *ImageRepository, *Tag);

	UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>(UProjectPackagingSettings::StaticClass()->GetDefaultObject());
	
	FString PathToServerBuild = FPaths::Combine(PackagingSettings->StagingDirectory.Path, FString("LinuxServer"));
	
	FString DockerFileContent;
	FString NewDockerFilePath = FPaths::Combine(ServerBuildPath, FPaths::GetCleanFilename(DockerFilePath));
	IPlatformFile::GetPlatformPhysical().CopyFile(*NewDockerFilePath, *DockerFilePath);
	FFileHelper::LoadFileToString(DockerFileContent, *NewDockerFilePath);
	DockerFileContent = DockerFileContent.Replace(*FString("<PROJECT_NAME>"), FApp::GetProjectName());
	FFileHelper::SaveStringToFile(DockerFileContent, *NewDockerFilePath);

	FString StartScriptContent;
	FString NewStartScriptPath = FPaths::Combine(ServerBuildPath, FPaths::GetCleanFilename(StartScriptPath));
	IPlatformFile::GetPlatformPhysical().CopyFile(*NewStartScriptPath, *StartScriptPath);
	FFileHelper::LoadFileToString(StartScriptContent, *NewStartScriptPath);
	StartScriptContent = StartScriptContent.Replace(*FString("<PROJECT_NAME>"), FApp::GetProjectName());
	FFileHelper::SaveStringToFile(StartScriptContent, *NewStartScriptPath);

	FString CommandLine = FString::Printf(TEXT("docker build -t \"%s\" \"%s\""), *_ImageName, *ServerBuildPath);
	UE_LOG(EdgegapLog, Log, TEXT("%s"), *CommandLine);
	IUCMDHelperModule::Get().CreateUcmdTask(CommandLine, LOCTEXT("DisplayName", "Docker"), LOCTEXT("ContainerizingProjectTaskName", "Containerizing server"), LOCTEXT("ContainerizingTaskName", "Containerizing"), FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject")), false, &OnContainerizeCallback);
}

void FEdgegapSettingsDetails::PushContainer(FString ImageName, FString RegistryURL, FString PrivateUsername, FString PrivateToken, bool LoggedIn)
{
	_ImageName = ImageName;
	_RegistryURL = RegistryURL;
	_PrivateUsername = PrivateUsername;
	_PrivateToken = PrivateToken;


	if (!LoggedIn)
	{
		DockerLogin(RegistryURL, PrivateUsername, PrivateToken);
		return;
	}

	FString CommandLine = FString::Printf(TEXT("docker image push %s"), *ImageName);
	IUCMDHelperModule::Get().CreateUcmdTask(CommandLine, LOCTEXT("DisplayName", "Docker"), LOCTEXT("PushContainerProjectTaskName", "Pushing Container"), LOCTEXT("ContainerizingTaskName", "Docker Login"), FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject")), false, &OnPushContainerCallback);
}

void FEdgegapSettingsDetails::DockerLogin(FString RegistryURL, FString PrivateUsername, FString PrivateToken)
{
	FString CommandLine = FString::Printf(TEXT("echo \'%s\' | docker login -u \'%s\' --password-stdin %s"), *PrivateToken, *PrivateUsername, *RegistryURL);
	UE_LOG(EdgegapLog, Log, TEXT("%s"), *CommandLine);
	IUCMDHelperModule::Get().CreateUcmdTask(CommandLine, LOCTEXT("DisplayName", "Docker"), LOCTEXT("DockerLoginProjectTaskName", "Logging into Registry"), LOCTEXT("ContainerizingTaskName", "Docker Login"), FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject")), true, &OnDockerLoginCallback);
}

void FEdgegapSettingsDetails::CreateApp(FString AppName, FString ImagePath, FString API_key)
{
	_API_key = API_key;
	_AppName = AppName;

	FString URL = "https://api.edgegap.com/v1/app";
	FHttpModule* Http = &FHttpModule::Get();
	if (!Http)
	{
		UE_LOG(EdgegapLog, Error, TEXT("Could not get a pointer to http module!"));
		return;
	}

	if (!FPaths::FileExists(ImagePath))
	{
		// check if the file is relative to the save/BouncedWavFiles directory
		UE_LOG(EdgegapLog, Error, TEXT("CreateApp: File does not exist!, %s"), *ImagePath);
		return;
	}
	// Read the file into a byte array
	TArray<uint8> Payload;
	FFileHelper::LoadFileToArray(Payload, *ImagePath, 0);
	FString EncodedImage = FBase64::Encode(Payload);

	// Create the request
	FHttpRequestRef Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindStatic(&FEdgegapSettingsDetails::onCreateAppComplete);

	// Set request fields
	Request->SetURL(URL);
	Request->SetVerb("POST");
	Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));

	Request->SetHeader("Content-Type", "application/json");
	Request->SetHeader(TEXT("Authorization"), *API_key);

	// prepare json data
	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue("name", AppName);
	JsonWriter->WriteValue("image", EncodedImage);
	JsonWriter->WriteValue("is_active", true);
	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();
	// Insert the content into the request
	Request->SetContentAsString(JsonString);

	if (!Request->ProcessRequest())
	{
		UE_LOG(EdgegapLog, Error, TEXT("CreateApp: Could not process HTTP request"));
		return;
	}
}

void FEdgegapSettingsDetails::onCreateAppComplete(FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bWasSuccessful)
{
	if (!bWasSuccessful || ResponsePtr->GetResponseCode() < 200 || ResponsePtr->GetResponseCode() > 299)
	{
		FString Response = ResponsePtr->GetContentAsString();
		UE_LOG(EdgegapLog, Warning, TEXT("onCreateAppComplete: HTTP request failed with code %d and response: %s"), ResponsePtr->GetResponseCode(), *Response);
		return;
	}

	FString Response = ResponsePtr->GetContentAsString();

	TSharedPtr<FJsonValue> JsonValue;
	// Create a reader pointer to read the json data
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

	if (FJsonSerializer::Deserialize(Reader, JsonValue)) 
	{
		if (JsonValue->AsObject()->HasField("message"))
		{
			FString message = JsonValue->AsObject()->GetStringField("message");
			UE_LOG(EdgegapLog, Error, TEXT("onCreateAppComplete: Failed, message:%s"), *message);
			return;
		}
	}
	else
	{
		UE_LOG(EdgegapLog, Error, TEXT("onCreateAppComplete: Could not deserialize response into Json, Response:%s"), *Response);
		return;
	}
}

void FEdgegapSettingsDetails::CreateVersion(FString AppName, FString VersionName, FString API_key, FString RegistryURL, FString ImageRepository, FString Tag, FString PrivateUsername, FString PrivateToken)
{
	_API_key = API_key;
	_AppName = AppName;
	_VersionName = VersionName;


	FString URL = FString::Printf(TEXT("https://api.edgegap.com/v1/app/%s/version"), *AppName);

	FHttpModule* Http = &FHttpModule::Get();
	if (!Http)
	{
		UE_LOG(EdgegapLog, Error, TEXT("Could not get a pointer to http module!"));
		return;
	}

	// Create the request
	FHttpRequestRef Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindStatic(&FEdgegapSettingsDetails::onCreateVersionComplete);

	// Set request fields
	Request->SetURL(URL);
	Request->SetVerb("POST");
	Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));

	Request->SetHeader("Content-Type", "application/json");
	Request->SetHeader(TEXT("Authorization"), *API_key);

	// prepare json data
	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue("name", VersionName);
	JsonWriter->WriteValue("docker_repository", RegistryURL);
	JsonWriter->WriteValue("docker_image", ImageRepository);
	JsonWriter->WriteValue("docker_tag", Tag);
	JsonWriter->WriteValue("private_username", PrivateUsername);
	JsonWriter->WriteValue("private_token", PrivateToken);
	JsonWriter->WriteValue("req_cpu", 128);
	JsonWriter->WriteValue("req_memory", 256);
	JsonWriter->WriteValue("req_video", 0);
	JsonWriter->WriteValue("max_duration", 60);
	JsonWriter->WriteValue("time_to_deploy", 120);
	JsonWriter->WriteValue("use_telemetry", false);
	JsonWriter->WriteValue("inject_context_env", true);
	JsonWriter->WriteValue("force_cache", false);
	JsonWriter->WriteValue("whitelisting_active", false);

	JsonWriter->WriteArrayStart(TEXT("ports"));
	JsonWriter->WriteObjectStart();

	JsonWriter->WriteValue("port", 0);
	JsonWriter->WriteValue("protocol", TEXT("TCP/UDP"));
	JsonWriter->WriteValue("to_check", false);
	JsonWriter->WriteValue("tls_upgrade", false);
	JsonWriter->WriteValue("name", TEXT("gameport"));

	JsonWriter->WriteObjectEnd();
	JsonWriter->WriteArrayEnd();

	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();
	// Insert the content into the request
	Request->SetContentAsString(JsonString);

	if (!Request->ProcessRequest())
	{
		UE_LOG(EdgegapLog, Error, TEXT("CreateVersion: Could not process HTTP request"));
		return;
	}
}

void FEdgegapSettingsDetails::onCreateVersionComplete(FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bWasSuccessful)
{
	if (!bWasSuccessful || ResponsePtr->GetResponseCode() < 200 || ResponsePtr->GetResponseCode() > 299)
	{
		FString Response = ResponsePtr->GetContentAsString();
		UE_LOG(EdgegapLog, Warning, TEXT("onCreateVersionComplete: HTTP request failed with code %d and response: %s"), ResponsePtr->GetResponseCode(), *Response);
		return;
	}

	FString Response = ResponsePtr->GetContentAsString();

	TSharedPtr<FJsonValue> JsonValue;
	// Create a reader pointer to read the json data
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

	if (FJsonSerializer::Deserialize(Reader, JsonValue))
	{
		if (JsonValue->AsObject()->HasField("message"))
		{
			FString message = JsonValue->AsObject()->GetStringField("message");
			UE_LOG(EdgegapLog, Error, TEXT("onCreateVersionComplete: Failed, message:%s"), *message);
			return;
		}
	}
	else
	{
		UE_LOG(EdgegapLog, Error, TEXT("onCreateVersionComplete: Could not deserialize response into Json, Response:%s"), *Response);
		return;
	}
}

void FEdgegapSettingsDetails::DeployApp(FString AppName, FString VersionName, FString API_key)
{
	_API_key = API_key;
	_AppName = AppName;
	_VersionName = VersionName;

	FString URL = "https://api.edgegap.com/v1/deploy";

	FHttpModule* Http = &FHttpModule::Get();
	if (!Http)
	{
		UE_LOG(EdgegapLog, Error, TEXT("Could not get a pointer to http module!"));
		return;
	}

	// Create the request
	FHttpRequestRef Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindStatic(&FEdgegapSettingsDetails::onDeployApp);

	// Set request fields
	Request->SetURL(URL);
	Request->SetVerb("POST");
	Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));

	Request->SetHeader("Content-Type", "application/json");
	Request->SetHeader(TEXT("Authorization"), *API_key);

	// prepare json data
	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR>> JsonWriter = TJsonWriterFactory<TCHAR>::Create(&JsonString);
	JsonWriter->WriteObjectStart();
	JsonWriter->WriteValue(TEXT("app_name"), AppName);
	JsonWriter->WriteValue(TEXT("version_name"), VersionName);

	// Hardcoded IPs are not optimal. Use user's IP for best location.
	JsonWriter->WriteRawJSONValue(TEXT("ip_list"), "[\
		\"159.8.69.249\",\
		\"5.10.64.236\",\
		\"159.8.69.244\",\
		\"89.29.103.62\"\
	]");

	JsonWriter->WriteObjectEnd();
	JsonWriter->Close();

	// Insert the content into the request
	Request->SetContentAsString(JsonString);

	if (!Request->ProcessRequest())
	{
		UE_LOG(EdgegapLog, Error, TEXT("CreateVersion: Could not process HTTP request"));
		return;
	}

}

void FEdgegapSettingsDetails::onDeployApp(FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bWasSuccessful)
{
	if (!bWasSuccessful || ResponsePtr->GetResponseCode() < 200 || ResponsePtr->GetResponseCode() > 299)
	{
		FString Response = ResponsePtr->GetContentAsString();
		UE_LOG(EdgegapLog, Warning, TEXT("onDeployApp: HTTP request failed with code %d and response: %s"), ResponsePtr->GetResponseCode(), *Response);
		return;
	}

	FString Response = ResponsePtr->GetContentAsString();

	TSharedPtr<FJsonValue> JsonValue;
	// Create a reader pointer to read the json data
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

	if (FJsonSerializer::Deserialize(Reader, JsonValue))
	{
		if (JsonValue->AsObject()->HasField("message"))
		{
			FString message = JsonValue->AsObject()->GetStringField("message");
			UE_LOG(EdgegapLog, Error, TEXT("onDeployApp: Failed, message:%s"), *message);
			return;
		}
	}
	else
	{
		UE_LOG(EdgegapLog, Error, TEXT("onDeployApp: Could not deserialize response into Json, Response:%s"), *Response);
		return;
	}
	GetDeploymentsInfo(FEdgegapSettingsDetails::GetInstance()->Settings->API_Key);
}

void FEdgegapSettingsDetails::GetDeploymentsInfo(FString API_key)
{
	_API_key = API_key;

	FString URL = "https://api.edgegap.com/v1/deployments";

	FHttpModule* Http = &FHttpModule::Get();
	if (!Http)
	{
		UE_LOG(EdgegapLog, Error, TEXT("Could not get a pointer to http module!"));
		return;
	}

	// Create the request
	FHttpRequestRef Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindStatic(&FEdgegapSettingsDetails::onGetDeploymentsInfo);

	// Set request fields
	Request->SetURL(URL);
	Request->SetVerb("GET");
	Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));

	Request->SetHeader("Content-Type", "application/json");
	Request->SetHeader(TEXT("Authorization"), *API_key);

	if (!Request->ProcessRequest())
	{
		UE_LOG(EdgegapLog, Error, TEXT("GetDeploymentsInfo: Could not process HTTP request"));
		return;
	}
}

void FEdgegapSettingsDetails::onGetDeploymentsInfo(FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bWasSuccessful)
{
	FEdgegapSettingsDetails::GetInstance()->DeploymentStatuRefreshButton->SetEnabled(true);

	FEdgegapSettingsDetails::GetInstance()->DeployStatusOverrideListSource.Empty();
	auto _settings = FEdgegapSettingsDetails::GetInstance()->Settings;

	if (!bWasSuccessful || ResponsePtr->GetResponseCode() < 200 || ResponsePtr->GetResponseCode() > 299)
	{
		FString Response = ResponsePtr->GetContentAsString();
		UE_LOG(EdgegapLog, Warning, TEXT("onGetDeploymentsInfo: HTTP request failed with code %d and response: %s"), ResponsePtr->GetResponseCode(), *Response);
		return;
	}

	FString Response = ResponsePtr->GetContentAsString();

	TSharedPtr<FJsonValue> JsonValue;
	// Create a reader pointer to read the json data
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);

	if (!FJsonSerializer::Deserialize(Reader, JsonValue))
	{
		UE_LOG(EdgegapLog, Error, TEXT("onDeployApp: Could not deserialize response into Json, Response:%s"), *Response);
		return;
	}

	if (!JsonValue->AsObject()->HasField("data"))
	{
		FString message = JsonValue->AsObject()->GetStringField("message");
		UE_LOG(EdgegapLog, Error, TEXT("onDeployApp: Failed, message:%s"), *message);
		return;
	}

	for (auto deployment : JsonValue->AsObject()->GetArrayField("data"))
	{
		FString link = deployment->AsObject()->GetObjectField("ports")->GetObjectField("gameport")->GetStringField("link");
		FString RequestID = deployment->AsObject()->GetStringField("request_id");
		FString Status = deployment->AsObject()->GetStringField("status");
		bool bReady = deployment->AsObject()->GetBoolField("ready");
		FEdgegapSettingsDetails::GetInstance()->DeployStatusOverrideListSource.Add(MakeShareable(new FDeploymentStatusListItem(link, Status, RequestID, _settings->API_Key, bReady)));
	}

	auto listView = FEdgegapSettingsDetails::GetInstance()->DeploymentStatusListItemListView;

	listView->SetListItemsSource(&FEdgegapSettingsDetails::GetInstance()->DeployStatusOverrideListSource);
}

void FEdgegapSettingsDetails::StopDeploy(FString RequestID, FString API_key)
{
	FString URL = FString::Printf(TEXT("https://api.edgegap.com/v1/stop/%s"), *RequestID);

	FHttpModule* Http = &FHttpModule::Get();
	if (!Http)
	{
		UE_LOG(EdgegapLog, Error, TEXT("Could not get a pointer to http module!"));
		return;
	}

	// Create the request
	FHttpRequestRef Request = Http->CreateRequest();
	Request->OnProcessRequestComplete().BindStatic(&FEdgegapSettingsDetails::onStopDeploy);

	// Set request fields
	Request->SetURL(URL);
	Request->SetVerb("DELETE");
	Request->SetHeader(TEXT("User-Agent"), TEXT("X-UnrealEngine-Agent"));

	Request->SetHeader(TEXT("Authorization"), *API_key);

	if (!Request->ProcessRequest())
	{
		UE_LOG(EdgegapLog, Error, TEXT("StopDeploy: Could not process HTTP request"));
		return;
	}
}

void FEdgegapSettingsDetails::onStopDeploy(FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bWasSuccessful)
{
	if (!bWasSuccessful || ResponsePtr->GetResponseCode() < 200 || ResponsePtr->GetResponseCode() > 299)
	{
		FString Response = ResponsePtr->GetContentAsString();
		UE_LOG(EdgegapLog, Warning, TEXT("onStopDeploy: HTTP request failed with code %d and response: %s"), ResponsePtr->GetResponseCode(), *Response);
		return;
	}

	GetDeploymentsInfo(FEdgegapSettingsDetails::GetInstance()->Settings->API_Key);
}

TSharedRef<ITableRow> FEdgegapSettingsDetails::HandleGenerateDeployStatusWidget(TSharedPtr<FDeploymentStatusListItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	UE_LOG(EdgegapLog, Log, TEXT("HandleGenerateDeployStatusWidget %s : %s : %s : %d"), *InItem->DeploymentIP, *InItem->DeploymentStatus, *InItem->RequestID, InItem->DeploymentReady);
	return SNew(SDeployStatusListItem, OwnerTable)
		.Item(InItem);
}


#undef LOCTEXT_NAMESPACE