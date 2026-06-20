#include "CsvLocalizator.h"
#include "Misc/ConfigCacheIni.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "IPythonScriptPlugin.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "FCsvLocalizatorModule"

namespace CsvLocalizator
{
    static const FName TabName(TEXT("CsvLocalizator"));
    static const FString AllCulturesOption = TEXT("All cultures in one CSV");

    static FString PyStringLiteral(const FString& Value)
    {
        FString Escaped = Value;
        Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
        Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
        Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
        Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
        return FString::Printf(TEXT("\"%s\""), *Escaped);
    }

    static void ExecuteToolCommand(const FString& FunctionName, const TMap<FString, FString>& Args)
    {
        FString Command = TEXT("import unreal_po_csv\n");
        Command += FString::Printf(TEXT("unreal_po_csv.%s("), *FunctionName);

        bool bFirst = true;
        for (const TPair<FString, FString>& Arg : Args)
        {
            if (!bFirst)
            {
                Command += TEXT(", ");
            }

            Command += Arg.Key;
            Command += TEXT("=");
            Command += PyStringLiteral(Arg.Value);
            bFirst = false;
        }

        Command += TEXT(")");
        IPythonScriptPlugin::Get()->ExecPythonCommand(*Command);
    }

class SCsvLocalizatorWindow : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCsvLocalizatorWindow) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        LocalizationRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Localization"));
        OutputPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("LocalizationCSV"));
        InputCsvPath = OutputPath / TEXT("Game_all.csv");
        NativeCulture = TEXT("en");

        RefreshTargetsAndCultures();

        ChildSlot
        [
            SNew(SScrollBox)
            + SScrollBox::Slot()
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    SNew(STextBlock).Text(LOCTEXT("Title", "CSV Localizator"))
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    MakePathRow(
                        LOCTEXT("LocalizationRoot", "Localization Root"),
                        SAssignNew(LocalizationRootTextBox, SEditableTextBox)
                        .Text(this, &SCsvLocalizatorWindow::GetLocalizationRootText)
                        .OnTextCommitted(this, &SCsvLocalizatorWindow::OnLocalizationRootCommitted),
                        FOnClicked::CreateSP(this, &SCsvLocalizatorWindow::ChooseLocalizationRoot)
                    )
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    MakeTargetRow()
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    MakeExportModeRow()
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    MakePathRow(
                        LOCTEXT("OutputCsv", "CSV Output Folder"),
                        SAssignNew(OutputPathTextBox, SEditableTextBox)
                        .Text(this, &SCsvLocalizatorWindow::GetOutputPathText)
                        .OnTextCommitted(this, &SCsvLocalizatorWindow::OnOutputPathCommitted),
                        FOnClicked::CreateSP(this, &SCsvLocalizatorWindow::ChooseOutputPath)
                    )
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    MakePathRow(
                        LOCTEXT("InputCsv", "CSV Input File"),
                        SAssignNew(InputCsvPathTextBox, SEditableTextBox)
                        .Text(this, &SCsvLocalizatorWindow::GetInputCsvPathText)
                        .OnTextCommitted(this, &SCsvLocalizatorWindow::OnInputCsvPathCommitted),
                        FOnClicked::CreateSP(this, &SCsvLocalizatorWindow::ChooseInputCsvPath)
                    )
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    SNew(SUniformGridPanel).SlotPadding(4)
                    + SUniformGridPanel::Slot(0, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Refresh", "Refresh"))
                        .OnClicked(this, &SCsvLocalizatorWindow::OnRefreshClicked)
                    ]
                    + SUniformGridPanel::Slot(1, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Export", "Export PO to CSV"))
                        .OnClicked(this, &SCsvLocalizatorWindow::OnExportClicked)
                    ]
                    + SUniformGridPanel::Slot(2, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("Import", "Import CSV to PO"))
                        .OnClicked(this, &SCsvLocalizatorWindow::OnImportClicked)
                    ]
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [
                    SNew(STextBlock)
                    .Text(this, &SCsvLocalizatorWindow::GetStatusText)
                    .AutoWrapText(true)
                ]
            ]
        ];
    }

    void RefreshNativeCulture()
    {
        if (!SelectedTarget.IsValid())
        {
            return;
        }

        const FString ConfigPath = FPaths::ProjectConfigDir() / TEXT("Localization") / (*SelectedTarget + TEXT(".ini"));

        FString FoundNativeCulture;
        if (GConfig && GConfig->GetString(TEXT("CommonSettings"), TEXT("NativeCulture"), FoundNativeCulture, ConfigPath))
        {
            NativeCulture = FoundNativeCulture;
        }
    }

private:
    FString LocalizationRoot;
    FString OutputPath;
    FString InputCsvPath;
    FString NativeCulture;
    FString Status;

    TArray<TSharedPtr<FString>> Targets;
    TArray<TSharedPtr<FString>> Cultures;
    TSharedPtr<FString> SelectedTarget;
    TSharedPtr<FString> SelectedCulture;

    TSharedPtr<SEditableTextBox> LocalizationRootTextBox;
    TSharedPtr<SEditableTextBox> OutputPathTextBox;
    TSharedPtr<SEditableTextBox> InputCsvPathTextBox;
    TSharedPtr<SEditableTextBox> NativeCultureTextBox;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> TargetComboBox;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> CultureComboBox;

    TSharedPtr<FString> FindOptionByValue(const TArray<TSharedPtr<FString>>& Options, const FString& Value) const
    {
        for (const TSharedPtr<FString>& Option : Options)
        {
            if (Option.IsValid() && *Option == Value)
            {
                return Option;
            }
        }

        return nullptr;
    }
    
    TSharedRef<SWidget> MakePathRow(const FText& Label, TSharedRef<SWidget> Input, FOnClicked BrowseAction)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [
                SNew(STextBlock).Text(Label).MinDesiredWidth(150)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                Input
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
            [
                SNew(SButton).Text(LOCTEXT("Browse", "...")).OnClicked(BrowseAction)
            ];
    }

    TSharedRef<SWidget> MakeTargetRow()
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [
                SNew(STextBlock).Text(LOCTEXT("Target", "Target")).MinDesiredWidth(150)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SAssignNew(TargetComboBox, SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&Targets)
                .OnGenerateWidget(this, &SCsvLocalizatorWindow::GenerateComboItem)
                .OnSelectionChanged(this, &SCsvLocalizatorWindow::OnTargetChanged)
                .InitiallySelectedItem(SelectedTarget)
                [
                    SNew(STextBlock).Text(this, &SCsvLocalizatorWindow::GetSelectedTargetText)
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 8, 0)
            [
                SNew(STextBlock).Text(LOCTEXT("NativeCulture", "Native Culture"))
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SAssignNew(NativeCultureTextBox, SEditableTextBox)
                .MinDesiredWidth(80)
                .Text(this, &SCsvLocalizatorWindow::GetNativeCultureText)
                .OnTextCommitted(this, &SCsvLocalizatorWindow::OnNativeCultureCommitted)
            ];
    }

    TSharedRef<SWidget> MakeExportModeRow()
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
            [
                SNew(STextBlock).Text(LOCTEXT("SelectedCulture", "Selected Culture")).MinDesiredWidth(150)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SAssignNew(CultureComboBox, SComboBox<TSharedPtr<FString>>)
                .OptionsSource(&Cultures)
                .OnGenerateWidget(this, &SCsvLocalizatorWindow::GenerateComboItem)
                .OnSelectionChanged(this, &SCsvLocalizatorWindow::OnCultureChanged)
                .InitiallySelectedItem(SelectedCulture)
                [
                    SNew(STextBlock).Text(this, &SCsvLocalizatorWindow::GetSelectedCultureText)
                ]
            ];
    }

    TSharedRef<SWidget> GenerateComboItem(TSharedPtr<FString> Item) const
    {
        return SNew(STextBlock).Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty());
    }

    void RefreshTargetsAndCultures()
    {
        const FString PreviousTarget = SelectedTarget.IsValid() ? *SelectedTarget : TEXT("");

        Targets.Empty();

        TArray<FString> Directories;
        IFileManager::Get().FindFiles(Directories, *(LocalizationRoot / TEXT("*")), false, true);

        for (const FString& Directory : Directories)
        {
            Targets.Add(MakeShared<FString>(Directory));
        }

        SelectedTarget = FindOptionByValue(Targets, PreviousTarget);

        if (!SelectedTarget.IsValid() && Targets.Num() > 0)
        {
            SelectedTarget = Targets[0];
        }

        RefreshNativeCulture();
        RefreshCultures();
    }

    void RefreshCultures()
    {
        Cultures.Empty();

        Cultures.Add(MakeShared<FString>(AllCulturesOption));

        if (!SelectedTarget.IsValid())
        {
            SelectedCulture = Cultures[0];
            return;
        }

        TArray<FString> Directories;
        IFileManager::Get().FindFiles(Directories, *(LocalizationRoot / *SelectedTarget / TEXT("*")), false, true);

        for (const FString& Directory : Directories)
        {
            const FString PoPath = LocalizationRoot / *SelectedTarget / Directory / (*SelectedTarget + TEXT(".po"));
            if (FPaths::FileExists(PoPath))
            {
                Cultures.Add(MakeShared<FString>(Directory));
            }
        }

        SelectedCulture = Cultures[0];
    }

    FText GetLocalizationRootText() const { return FText::FromString(LocalizationRoot); }
    FText GetOutputPathText() const { return FText::FromString(OutputPath); }
    FText GetInputCsvPathText() const { return FText::FromString(InputCsvPath); }
    FText GetNativeCultureText() const { return FText::FromString(NativeCulture); }
    FText GetStatusText() const { return FText::FromString(Status); }
    FText GetSelectedTargetText() const { return SelectedTarget.IsValid() ? FText::FromString(*SelectedTarget) : LOCTEXT("NoTarget", "No target found"); }
    FText GetSelectedCultureText() const { return SelectedCulture.IsValid() ? FText::FromString(*SelectedCulture) : LOCTEXT("NoCulture", "No culture found"); }

    void OnLocalizationRootCommitted(const FText& Text, ETextCommit::Type) { LocalizationRoot = Text.ToString(); RefreshTargetsAndCultures(); }
    void OnOutputPathCommitted(const FText& Text, ETextCommit::Type) { OutputPath = Text.ToString(); }
    void OnInputCsvPathCommitted(const FText& Text, ETextCommit::Type) { InputCsvPath = Text.ToString(); }
    void OnNativeCultureCommitted(const FText& Text, ETextCommit::Type) { NativeCulture = Text.ToString(); RefreshCultures(); }

    void OnCultureChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type)
    {
        if (!NewValue.IsValid())
        {
            return;
        }

        SelectedCulture = NewValue;
    }

    void OnTargetChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type)
    {
        if (!NewValue.IsValid())
        {
            return;
        }

        SelectedTarget = NewValue;

        RefreshNativeCulture();
        RefreshCultures();

        if (CultureComboBox.IsValid())
        {
            CultureComboBox->RefreshOptions();
            CultureComboBox->SetSelectedItem(SelectedCulture);
        }
    }

    FReply ChooseLocalizationRoot()
    {
        ChooseDirectory(LocalizationRoot, TEXT("Choose Localization Root"));
        RefreshTargetsAndCultures();
        return FReply::Handled();
    }

    FReply ChooseOutputPath()
    {
        ChooseDirectory(OutputPath, TEXT("Choose CSV Output Folder"));
        return FReply::Handled();
    }

    FReply ChooseInputCsvPath()
    {
        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
        if (!DesktopPlatform)
        {
            return FReply::Handled();
        }

        TArray<FString> Files;
        const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
        if (DesktopPlatform->OpenFileDialog(ParentWindow, TEXT("Choose CSV Input"), OutputPath, TEXT(""), TEXT("CSV files (*.csv)|*.csv"), EFileDialogFlags::None, Files) && Files.Num() > 0)
        {
            InputCsvPath = Files[0];
        }

        return FReply::Handled();
    }

    bool ChooseDirectory(FString& InOutPath, const FString& Title)
    {
        IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
        if (!DesktopPlatform)
        {
            return false;
        }

        FString ChosenDirectory;
        const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
        if (DesktopPlatform->OpenDirectoryDialog(ParentWindow, Title, InOutPath, ChosenDirectory))
        {
            InOutPath = ChosenDirectory;
            return true;
        }
        return false;
    }

    FReply OnRefreshClicked()
    {
        RefreshTargetsAndCultures();
        if (TargetComboBox.IsValid())
        {
            TargetComboBox->RefreshOptions();
            TargetComboBox->SetSelectedItem(SelectedTarget);
        }
        if (CultureComboBox.IsValid())
        {
            CultureComboBox->RefreshOptions();
            CultureComboBox->SetSelectedItem(SelectedCulture);
        }
        Status = FString::Printf(TEXT("Found %d target(s), %d culture(s)."), Targets.Num(), Cultures.Num());
        return FReply::Handled();
    }

    FReply OnExportClicked()
    {
        if (!SelectedTarget.IsValid())
        {
            Status = TEXT("No localization target selected.");
            return FReply::Handled();
        }

        const bool bAllCulturesSelected = SelectedCulture.IsValid() && *SelectedCulture == AllCulturesOption;

        ExecuteToolCommand(TEXT("export_to_csv"), {
            {TEXT("localization_root"), LocalizationRoot},
            {TEXT("target"), *SelectedTarget},
            {TEXT("native_culture"), NativeCulture},
            {TEXT("mode"), bAllCulturesSelected ? TEXT("all") : TEXT("selected")},
            {TEXT("selected_culture"), bAllCulturesSelected ? TEXT("") : (SelectedCulture.IsValid() ? *SelectedCulture : TEXT(""))},
            {TEXT("output_path"), OutputPath}
        });
        Status = TEXT("Export command sent. Check Output Log for details.");
        return FReply::Handled();
    }

    FReply OnImportClicked()
    {
        if (!SelectedTarget.IsValid())
        {
            Status = TEXT("No localization target selected.");
            return FReply::Handled();
        }

        ExecuteToolCommand(TEXT("import_from_csv"), {
            {TEXT("localization_root"), LocalizationRoot},
            {TEXT("target"), *SelectedTarget},
            {TEXT("native_culture"), NativeCulture},
            {TEXT("csv_path"), InputCsvPath}
        });
        Status = TEXT("Import command sent. Check Output Log for details.");
        return FReply::Handled();
    }
};
}

class FCsvLocalizatorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        StyleSet = MakeShared<FSlateStyleSet>("CsvLocalizatorStyle");

        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CsvLocalizator"));
        if (Plugin.IsValid())
        {
            const FString ResourcesDir = Plugin->GetBaseDir() / TEXT("Resources");
            StyleSet->SetContentRoot(ResourcesDir);

            StyleSet->Set(
                "CsvLocalizator.TabIcon",
                new FSlateImageBrush(
                    StyleSet->RootToContentDir(TEXT("plugin_icon"), TEXT(".png")),
                    FVector2D(16.0f, 16.0f)
                )
            );

            FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
        }
        
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            CsvLocalizator::TabName,
            FOnSpawnTab::CreateRaw(this, &FCsvLocalizatorModule::SpawnPluginTab)
        )
        .SetDisplayName(LOCTEXT("TabTitle", "CSV Localizator"))
        .SetIcon(FSlateIcon("CsvLocalizatorStyle", "CsvLocalizator.TabIcon"))
        .SetMenuType(ETabSpawnerMenuType::Hidden);

        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCsvLocalizatorModule::RegisterMenus)
        );
    }

    virtual void ShutdownModule() override
    {
        if (StyleSet.IsValid())
        {
            FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
            StyleSet.Reset();
        }
        
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CsvLocalizator::TabName);
    }

private:
    TSharedRef<SDockTab> SpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(CsvLocalizator::SCsvLocalizatorWindow)
            ];
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped OwnerScoped(this);

        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");

        FToolMenuSection& Section = Menu->FindOrAddSection("Tools");

        Section.AddMenuEntry(
            "OpenCsvLocalizator",
            LOCTEXT("OpenMenuLabel", "CSV Localizator"),
            LOCTEXT("OpenMenuTooltip", "Open the CSV Localizator tool"),
            FSlateIcon("CsvLocalizatorStyle", "CsvLocalizator.TabIcon"),
            FUIAction(FExecuteAction::CreateRaw(this, &FCsvLocalizatorModule::OpenPluginWindow))
        );
    }

    void OpenPluginWindow()
    {
        FGlobalTabmanager::Get()->TryInvokeTab(CsvLocalizator::TabName);
    }

private:
    TSharedPtr<FSlateStyleSet> StyleSet;
    
};

IMPLEMENT_MODULE(FCsvLocalizatorModule, CsvLocalizator)

#undef LOCTEXT_NAMESPACE
