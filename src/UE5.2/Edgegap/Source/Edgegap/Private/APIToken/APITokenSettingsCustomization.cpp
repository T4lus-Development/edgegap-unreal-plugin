#include "APITokenSettingsCustomization.h"
#include "APITokenSettings.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "JsonObjectConverter.h"
#include "Widgets/SBoxPanel.h"

FAPITokenSettingsCustomization::FAPITokenSettingsCustomization()
{

}

TSharedRef<IPropertyTypeCustomization> FAPITokenSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FAPITokenSettingsCustomization());
}

void FAPITokenSettingsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	const FSlateColor BlueSlateColor = FSlateColor(FLinearColor(0, 0.23f, 0.65f, 1.0f));

	TSharedPtr<IPropertyHandle> PropertyChildHandle = PropertyHandle->GetChildHandle("APIToken");

	FString CurrentAPITokenStr;
	PropertyChildHandle->GetValue(CurrentAPITokenStr);

	HeaderRow
		.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
		/*
		SNew(STextBlock)
		.Text(LOCTEXT("API Token", "API Token"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.MinDesiredWidth(80.f)
		*/
	]
		.ValueContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(2)
		.AutoWidth()
		[
			SNew(SEditableTextBox)
			.MinDesiredWidth(80.f)
			.Text_Lambda([this, PropertyChildHandle]() -> const FText {
				FString APITokenStr;
				PropertyChildHandle->GetValue(APITokenStr);
				// PropertyChildHandle->GetValueAsFormattedString(APITokenStr);
				//UE_LOG(EdgegapLog, Log, TEXT("FAPITokenSettingsCustomization---Text_Lambda %s"), *APITokenStr);

				return FText::FromString(APITokenStr);
			})
			.OnTextCommitted_Lambda([this, PropertyChildHandle](const FText& InText, ETextCommit::Type CommitType) {
				if (CommitType == ETextCommit::OnEnter)
				{
					FString NewAPIKey = InText.ToString();
					//UE_LOG(EdgegapLog, Log, TEXT("FAPITokenSettingsCustomization---OnTextCommitted_Lambda %s"), *NewAPIKey);

					PropertyChildHandle->SetValue(NewAPIKey);
				}
			})		
		]
		+ SHorizontalBox::Slot()
		.Padding(2)
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("Verify", "Verify"))
			.ToolTipText(LOCTEXT("Verify_Tooltip", "Verifies the token"))
			// .ButtonColorAndOpacity(BlueSlateColor)
			.ForegroundColor(BlueSlateColor)
			.IsEnabled_Lambda([PropertyChildHandle]() -> bool
			{
				FString _CurrentAPITokenStr;
				PropertyChildHandle->GetValue(_CurrentAPITokenStr);

				return !_CurrentAPITokenStr.IsEmpty();
			})
			.OnClicked_Lambda([this]()
			{
				FEdgegapSettingsDetails::GetInstance()->Request_VerifyToken();
				return(FReply::Handled());
			})
		]
		+ SHorizontalBox::Slot()
		.Padding(2)
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("GetAToken", "Get a Token"))
			.ToolTipText(LOCTEXT("GetAToken_Tooltip", "Gets a new token"))
			.OnClicked_Lambda([this]()
			{
				FPlatformProcess::LaunchURL(TEXT("https://app.edgegap.com/?oneClick=true"), NULL, NULL);

				return(FReply::Handled());
			})
		]
	];
}

void FAPITokenSettingsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{

}
