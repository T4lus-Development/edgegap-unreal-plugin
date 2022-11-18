using UnrealBuildTool;
using System.IO;



public class Edgegap : ModuleRules
{
	
	private string ModulePath
	{

        get
        {
            return System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory, "../../"));
        }
	}


	
	public Edgegap(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "ApplicationCore",
                "Engine",
                "EngineSettings",
                "InputCore",
                "RHI",
                "RenderCore",
                "Slate",
                "SlateCore",
                "EditorStyle",
                "SourceControl",
                "SourceControlWindows",
                "TargetPlatform",
                "DesktopPlatform",
                "UnrealEd",
                "WorkspaceMenuStructure",
                "MessageLog",
                "UATHelper",
                "TranslationEditor",
                "Projects",
                "DeviceProfileEditor",
                "UndoHistory",
                "Analytics",
                "ToolMenus",
                "LauncherServices",
                "DerivedDataCache",
                "HTTP",
                "Json",
                "JsonUtilities",
                "ImageWrapper",
                "SharedSettingsWidgets"
            }
        );

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "AssetTools",
                "DesktopPlatform",
                "GameProjectGeneration",
                "ProjectTargetPlatformEditor",
                "LevelEditor",
                "Settings",
                "SourceCodeAccess",
                "Toolbox",
                "LocalizationDashboard",
                "UCMDHelper",
                "Projects"
            }
        );
        System.Console.WriteLine("ModulePath here: ");
        System.Console.WriteLine(ModulePath);
        PublicDefinitions.Add("EDGEGAP_MODULE_PATH=\""+ ModulePath.Replace("\\", "/")+"\"");

        //// Get the engine path. Ends with "Engine/"
        //string engine_path = Path.GetFullPath(BuildConfiguration.RelativeEnginePath);
        //// Now get the base of UE4's modules dir (could also be Developer, Editor, ThirdParty)
        //string srcrt_path = engine_path + "Source/Editor/";

        //PublicIncludePaths.Add(srcrt_path + "MainFrame/Private/Frame");

        //if (Target.bBuildEditor)
        //{
        //	PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd" });
        //}

    }
}
