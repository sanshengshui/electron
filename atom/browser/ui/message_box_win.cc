// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/ui/message_box.h"

#include <windows.h>  // windows.h must be included first

#include <commctrl.h>

#include <map>
#include <vector>

#include "atom/browser/browser.h"
#include "atom/browser/native_window_views.h"
#include "atom/browser/unresponsive_suppressor.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/post_task.h"
#include "base/threading/thread.h"
#include "base/win/scoped_gdi_object.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "ui/gfx/icon_util.h"
#include "ui/gfx/image/image_skia.h"

namespace atom {

MessageBoxSettings::MessageBoxSettings() = default;
MessageBoxSettings::MessageBoxSettings(const MessageBoxSettings&) = default;
MessageBoxSettings::~MessageBoxSettings() = default;

namespace {

// Small command ID values are already taken by Windows, we have to start from
// a large number to avoid conflicts with Windows.
const int kIDStart = 100;

// Get the common ID from button's name.
struct CommonButtonID {
  int button;
  int id;
};
CommonButtonID GetCommonID(const base::string16& button) {
  base::string16 lower = base::ToLowerASCII(button);
  if (lower == L"ok")
    return {TDCBF_OK_BUTTON, IDOK};
  else if (lower == L"yes")
    return {TDCBF_YES_BUTTON, IDYES};
  else if (lower == L"no")
    return {TDCBF_NO_BUTTON, IDNO};
  else if (lower == L"cancel")
    return {TDCBF_CANCEL_BUTTON, IDCANCEL};
  else if (lower == L"retry")
    return {TDCBF_RETRY_BUTTON, IDRETRY};
  else if (lower == L"close")
    return {TDCBF_CLOSE_BUTTON, IDCLOSE};
  return {-1, -1};
}

// Determine whether the buttons are common buttons, if so map common ID
// to button ID.
void MapToCommonID(const std::vector<base::string16>& buttons,
                   std::map<int, int>* id_map,
                   TASKDIALOG_COMMON_BUTTON_FLAGS* button_flags,
                   std::vector<TASKDIALOG_BUTTON>* dialog_buttons) {
  for (size_t i = 0; i < buttons.size(); ++i) {
    auto common = GetCommonID(buttons[i]);
    if (common.button != -1) {
      // It is a common button.
      (*id_map)[common.id] = i;
      (*button_flags) |= common.button;
    } else {
      // It is a custom button.
      dialog_buttons->push_back(
          {static_cast<int>(i + kIDStart), buttons[i].c_str()});
    }
  }
}

int ShowTaskDialogUTF16(NativeWindow* parent,
                        MessageBoxType type,
                        const std::vector<base::string16>& buttons,
                        int default_id,
                        int cancel_id,
                        int options,
                        const base::string16& title,
                        const base::string16& message,
                        const base::string16& detail,
                        const base::string16& checkbox_label,
                        bool* checkbox_checked,
                        const gfx::ImageSkia& icon) {
  TASKDIALOG_FLAGS flags =
      TDF_SIZE_TO_CONTENT |           // Show all content.
      TDF_ALLOW_DIALOG_CANCELLATION;  // Allow canceling the dialog.

  TASKDIALOGCONFIG config = {0};
  config.cbSize = sizeof(config);
  config.hInstance = GetModuleHandle(NULL);
  config.dwFlags = flags;

  if (parent) {
    config.hwndParent =
        static_cast<atom::NativeWindowViews*>(parent)->GetAcceleratedWidget();
  }

  if (default_id > 0)
    config.nDefaultButton = kIDStart + default_id;

  // TaskDialogIndirect doesn't allow empty name, if we set empty title it
  // will show "electron.exe" in title.
  base::string16 app_name = base::UTF8ToUTF16(Browser::Get()->GetName());
  if (title.empty())
    config.pszWindowTitle = app_name.c_str();
  else
    config.pszWindowTitle = title.c_str();

  base::win::ScopedHICON hicon;
  if (!icon.isNull()) {
    hicon = IconUtil::CreateHICONFromSkBitmap(*icon.bitmap());
    config.dwFlags |= TDF_USE_HICON_MAIN;
    config.hMainIcon = hicon.get();
  } else {
    // Show icon according to dialog's type.
    switch (type) {
      case MessageBoxType::kInformation:
      case MessageBoxType::kQuestion:
        config.pszMainIcon = TD_INFORMATION_ICON;
        break;
      case MessageBoxType::kWarning:
        config.pszMainIcon = TD_WARNING_ICON;
        break;
      case MessageBoxType::kError:
        config.pszMainIcon = TD_ERROR_ICON;
        break;
      case MessageBoxType::kNone:
        break;
    }
  }

  // If "detail" is empty then don't make message hilighted.
  if (detail.empty()) {
    config.pszContent = message.c_str();
  } else {
    config.pszMainInstruction = message.c_str();
    config.pszContent = detail.c_str();
  }

  if (!checkbox_label.empty()) {
    config.pszVerificationText = checkbox_label.c_str();

    if (checkbox_checked && *checkbox_checked) {
      config.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;
    }
  }

  // Iterate through the buttons, put common buttons in dwCommonButtons
  // and custom buttons in pButtons.
  std::map<int, int> id_map;
  std::vector<TASKDIALOG_BUTTON> dialog_buttons;
  if (options & MESSAGE_BOX_NO_LINK) {
    for (size_t i = 0; i < buttons.size(); ++i)
      dialog_buttons.push_back(
          {static_cast<int>(i + kIDStart), buttons[i].c_str()});
  } else {
    MapToCommonID(buttons, &id_map, &config.dwCommonButtons, &dialog_buttons);
  }
  if (dialog_buttons.size() > 0) {
    config.pButtons = &dialog_buttons.front();
    config.cButtons = dialog_buttons.size();
    if (!(options & MESSAGE_BOX_NO_LINK))
      config.dwFlags |= TDF_USE_COMMAND_LINKS;  // custom buttons as links.
  }

  int id = 0;
  BOOL verificationFlagChecked = FALSE;
  TaskDialogIndirect(&config, &id, nullptr, &verificationFlagChecked);
  if (checkbox_checked) {
    *checkbox_checked = verificationFlagChecked;
  }

  if (id_map.find(id) != id_map.end())  // common button.
    return id_map[id];
  else if (id >= kIDStart)  // custom button.
    return id - kIDStart;
  else
    return cancel_id;
}

int ShowTaskDialogUTF8(const MessageBoxSettings& settings) {
  std::vector<base::string16> utf16_buttons;
  for (const auto& button : settings.buttons)
    utf16_buttons.push_back(base::UTF8ToUTF16(button));

  const base::string16 title_16 = base::UTF8ToUTF16(settings.title);
  const base::string16 message_16 = base::UTF8ToUTF16(settings.message);
  const base::string16 detail_16 = base::UTF8ToUTF16(settings.detail);
  const base::string16 checkbox_label_16 =
      base::UTF8ToUTF16(settings.checkbox_label);
  bool cb_checked = settings.checkbox_checked;

  return ShowTaskDialogUTF16(
      settings.parent_window, settings.type, utf16_buttons, settings.default_id,
      settings.cancel_id, settings.options, title_16, message_16, detail_16,
      checkbox_label_16, &cb_checked, settings.icon);
}

void RunMessageBoxInNewThread(base::Thread* thread,
                              const MessageBoxSettings& settings,
                              MessageBoxCallback callback) {
  int result = ShowTaskDialogUTF8(settings);
  base::PostTaskWithTraits(
      FROM_HERE, {content::BrowserThread::UI},
      base::BindOnce(std::move(callback), result, settings.checkbox_checked));
  content::BrowserThread::DeleteSoon(content::BrowserThread::UI, FROM_HERE,
                                     thread);
}

}  // namespace

int ShowMessageBoxSync(const MessageBoxSettings& settings) {
  atom::UnresponsiveSuppressor suppressor;
  return ShowTaskDialogUTF8(settings);
}

void ShowMessageBox(const MessageBoxSettings& settings,
                    MessageBoxCallback callback) {
  auto thread =
      std::make_unique<base::Thread>(ELECTRON_PRODUCT_NAME "MessageBoxThread");
  thread->init_com_with_mta(false);
  if (!thread->Start()) {
    std::move(callback).Run(settings.cancel_id, settings.checkbox_checked);
    return;
  }

  base::Thread* unretained = thread.release();
  unretained->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&RunMessageBoxInNewThread, base::Unretained(unretained),
                     settings, std::move(callback)));
}

void ShowErrorBox(const base::string16& title, const base::string16& content) {
  atom::UnresponsiveSuppressor suppressor;
  ShowTaskDialogUTF16(nullptr, MessageBoxType::kError, {}, -1, 0, 0, L"Error",
                      title, content, L"", nullptr, gfx::ImageSkia());
}

}  // namespace atom
