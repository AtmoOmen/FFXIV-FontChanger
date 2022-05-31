#include "pch.h"
#include "resource.h"
#include "Structs.h"
#include "ExportPreviewWindow.h"
#include "FaceElementEditorDialog.h"
#include "MainWindow.h"
#include "ProgressDialog.h"

struct ListViewCols {
	enum : int {
		FamilyName,
		SubfamilyName,
		Size,
		LineHeight,
		Ascent,
		HorizontalOffset,
		LetterSpacing,
		Gamma,
		Codepoints,
		GlyphCount,
		MergeMode,
		Renderer,
		Lookup,
	};
};

static constexpr auto FaceListBoxWidth = 160;
static constexpr auto ListViewHeight = 160;
static constexpr auto EditHeight = 40;

static constexpr GUID Guid_IFileDialog_Json{ 0x5c2fc703, 0x7406, 0x4704, {0x92, 0x12, 0xae, 0x41, 0x1d, 0x4b, 0x74, 0x67} };
static constexpr GUID Guid_IFileDialog_Export{ 0x5c2fc703, 0x7406, 0x4704, {0x92, 0x12, 0xae, 0x41, 0x1d, 0x4b, 0x74, 0x68} };

App::FontEditorWindow::FontEditorWindow(std::vector<std::wstring> args) : m_args(args) {
	WNDCLASSEXW wcex{};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.hInstance = g_hInstance;
	wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wcex.hbrBackground = GetStockBrush(WHITE_BRUSH);
	wcex.lpszClassName = ClassName;
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDR_FONTEDITOR);
	wcex.lpfnWndProc = FontEditorWindow::WndProcInitial;

	RegisterClassExW(&wcex);

	CreateWindowExW(0, ClassName, L"Font Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1200, 640,
		nullptr, nullptr, nullptr, this);
}

App::FontEditorWindow::~FontEditorWindow() = default;

bool App::FontEditorWindow::ConsumeDialogMessage(MSG & msg) {
	if (IsDialogMessage(m_hWnd, &msg))
		return true;

	for (const auto& e : m_editors | std::views::values)
		if (e && e->IsOpened() && e->ConsumeDialogMessage(msg))
			return true;

	return false;
}

bool App::FontEditorWindow::ConsumeAccelerator(MSG & msg) {
	if (!m_hAccelerator)
		return false;

	if (GetForegroundWindow() != m_hWnd)
		return false;

	if (msg.message == WM_KEYDOWN && msg.hwnd == m_hEdit) {
		if (msg.wParam == VK_RETURN || msg.wParam == VK_INSERT || msg.wParam == VK_DELETE)
			return false;
		if (!(GetKeyState(VK_CONTROL) & 0x8000) || msg.wParam == 'C' || msg.wParam == 'X' || msg.wParam == 'V' || msg.wParam == 'A')
			return false;
	}
	return TranslateAccelerator(m_hWnd, m_hAccelerator, &msg);
}

LRESULT App::FontEditorWindow::Window_OnCreate(HWND hwnd) {
	m_hWnd = hwnd;

	m_hAccelerator = LoadAcceleratorsW(g_hInstance, MAKEINTRESOURCEW(IDR_ACCELERATOR_FACEELEMENTEDITOR));

	NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
	SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
	m_hUiFont = CreateFontIndirectW(&ncm.lfMessageFont);

	m_hFacesListBox = CreateWindowExW(0, WC_LISTBOXW, nullptr,
		WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
		0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_FaceListBox), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
	m_hFaceElementsListView = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
		WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
		0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_FaceElementListView), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);
	m_hEdit = CreateWindowExW(0, WC_EDITW, nullptr,
		WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
		0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(Id_Edit), reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hWnd, GWLP_HINSTANCE)), nullptr);

	ListView_SetExtendedListViewStyle(m_hFaceElementsListView, LVS_EX_FULLROWSELECT);

	SendMessage(m_hFacesListBox, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
	SendMessage(m_hFaceElementsListView, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);
	SendMessage(m_hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_hUiFont), FALSE);

	SetWindowSubclass(m_hEdit, [](HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) -> LRESULT {
		if (msg == WM_GETDLGCODE && wParam == VK_TAB)
			return 0;
		if (msg == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && !(GetKeyState(VK_LWIN) & 0x8000) && !(GetKeyState(VK_RWIN) & 0x8000))
			Edit_SetSel(hWnd, 0, Edit_GetTextLength(hWnd));
		return DefSubclassProc(hWnd, msg, wParam, lParam);
	}, 1, 0);

	const auto AddColumn = [this](int columnIndex, int width, const wchar_t* name) {
		LVCOLUMNW col{
			.mask = LVCF_TEXT | LVCF_WIDTH,
			.cx = width,
			.pszText = const_cast<wchar_t*>(name),
		};
		ListView_InsertColumn(m_hFaceElementsListView, columnIndex, &col);
	};
	AddColumn(ListViewCols::FamilyName, 120, L"Family");
	AddColumn(ListViewCols::SubfamilyName, 80, L"Subfamily");
	AddColumn(ListViewCols::Size, 80, L"Size");
	AddColumn(ListViewCols::LineHeight, 80, L"Line Height");
	AddColumn(ListViewCols::Ascent, 80, L"Ascent");
	AddColumn(ListViewCols::HorizontalOffset, 120, L"Horizontal Offset");
	AddColumn(ListViewCols::LetterSpacing, 100, L"Letter Spacing");
	AddColumn(ListViewCols::Gamma, 60, L"Gamma");
	AddColumn(ListViewCols::Codepoints, 80, L"Codepoints");
	AddColumn(ListViewCols::MergeMode, 70, L"Overwrite");
	AddColumn(ListViewCols::GlyphCount, 60, L"Glyphs");
	AddColumn(ListViewCols::Renderer, 180, L"Renderer");
	AddColumn(ListViewCols::Lookup, 300, L"Lookup");

	if (m_args.size() >= 2 && std::filesystem::exists(m_args[1])) {
		try {
			SetCurrentFontSet(m_args[1]);
		} catch (const std::exception& e) {
			MessageBoxW(m_hWnd, std::format(L"Failed to open file: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
		}
	}
	if (m_path.empty())
		Menu_File_New(XivRes::GameFontType::font);

	Window_OnSize();
	ShowWindow(m_hWnd, SW_SHOW);
	return 0;
}

LRESULT App::FontEditorWindow::Window_OnSize() {
	RECT rc;
	GetClientRect(m_hWnd, &rc);

	auto hdwp = BeginDeferWindowPos(Id__Last);
	hdwp = DeferWindowPos(hdwp, m_hFacesListBox, nullptr, 0, 0, FaceListBoxWidth, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
	hdwp = DeferWindowPos(hdwp, m_hFaceElementsListView, nullptr, FaceListBoxWidth, 0, (std::max<int>)(0, rc.right - rc.left - FaceListBoxWidth), ListViewHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	hdwp = DeferWindowPos(hdwp, m_hEdit, nullptr, FaceListBoxWidth, ListViewHeight, (std::max<int>)(0, rc.right - rc.left - FaceListBoxWidth), EditHeight, SWP_NOZORDER | SWP_NOACTIVATE);
	EndDeferWindowPos(hdwp);

	m_nDrawLeft = FaceListBoxWidth;
	m_nDrawTop = static_cast<int>(EditHeight + ListViewHeight);
	m_bNeedRedraw = true;

	return 0;
}

LRESULT App::FontEditorWindow::Window_OnPaint() {
	union {
		struct {
			BITMAPINFOHEADER bmih;
			DWORD bitfields[3];
		};
		BITMAPINFO bmi{};
	};

	PAINTSTRUCT ps;
	const auto hdc = BeginPaint(m_hWnd, &ps);
	if (m_bNeedRedraw) {
		m_bNeedRedraw = false;

		RECT rc;
		GetClientRect(m_hWnd, &rc);
		m_pMipmap = std::make_shared<XivRes::MemoryMipmapStream>(
			(std::max<int>)(1, (rc.right - rc.left - m_nDrawLeft + m_nZoom - 1) / m_nZoom),
			(std::max<int>)(1, (rc.bottom - rc.top - m_nDrawTop + m_nZoom - 1) / m_nZoom),
			1,
			XivRes::TextureFormat::A8R8G8B8);

		const auto pad = 16 / m_nZoom;
		const auto buf = m_pMipmap->View<XivRes::RGBA8888>();
		std::ranges::fill(buf, XivRes::RGBA8888{ 0x88, 0x88, 0x88, 0xFF });

		for (int y = pad; y < m_pMipmap->Height - pad; y++) {
			for (int x = pad; x < m_pMipmap->Width - pad; x++)
				buf[y * m_pMipmap->Width + x] = { 0x00, 0x00, 0x00, 0xFF };
		}

		if (m_pActiveFace) {
			auto& face = *m_pActiveFace;

			const auto& mergedFont = *face.GetMergedFont();

			if (int lineHeight = mergedFont.GetLineHeight(), ascent = mergedFont.GetAscent(); lineHeight > 0 && m_bShowLineMetrics) {
				if (ascent < lineHeight) {
					for (int y = pad, y_ = m_pMipmap->Height - pad; y < y_; y += lineHeight) {
						for (int y2 = y + ascent, y2_ = (std::min)(y_, y + lineHeight); y2 < y2_; y2++)
							for (int x = pad; x < m_pMipmap->Width - pad; x++)
								buf[y2 * m_pMipmap->Width + x] = { 0x33, 0x33, 0x33, 0xFF };
					}
				} else if (ascent == lineHeight) {
					for (int y = pad, y_ = m_pMipmap->Height - pad; y < y_; y += 2 * lineHeight) {
						for (int y2 = y + lineHeight, y2_ = (std::min)(y_, y + 2 * lineHeight); y2 < y2_; y2++)
							for (int x = pad; x < m_pMipmap->Width - pad; x++)
								buf[y2 * m_pMipmap->Width + x] = { 0x33, 0x33, 0x33, 0xFF };
					}
				}
			}

			if (!face.PreviewText.empty()) {
				XivRes::FontGenerator::TextMeasurer(mergedFont)
					.WithMaxWidth(m_bWordWrap ? m_pMipmap->Width - pad * 2 : (std::numeric_limits<int>::max)())
					.WithUseKerning(m_bKerning)
					.Measure(face.PreviewText)
					.DrawTo(*m_pMipmap, mergedFont, pad, pad, { 0xFF, 0xFF, 0xFF, 0xFF }, { 0, 0, 0, 0 });
			}
		}
	}

	bmih.biSize = sizeof bmih;
	bmih.biWidth = m_pMipmap->Width;
	bmih.biHeight = -m_pMipmap->Height;
	bmih.biPlanes = 1;
	bmih.biBitCount = 32;
	bmih.biCompression = BI_BITFIELDS;
	reinterpret_cast<XivRes::RGBA8888*>(&bitfields[0])->SetFrom(255, 0, 0, 0);
	reinterpret_cast<XivRes::RGBA8888*>(&bitfields[1])->SetFrom(0, 255, 0, 0);
	reinterpret_cast<XivRes::RGBA8888*>(&bitfields[2])->SetFrom(0, 0, 255, 0);
	RECT rc;
	GetClientRect(m_hWnd, &rc);
	StretchDIBits(hdc, m_nDrawLeft, m_nDrawTop, m_pMipmap->Width * m_nZoom, m_pMipmap->Height * m_nZoom, 0, 0, m_pMipmap->Width, m_pMipmap->Height, &m_pMipmap->View<XivRes::RGBA8888>()[0], &bmi, DIB_RGB_COLORS, SRCCOPY);
	EndPaint(m_hWnd, &ps);

	return 0;
}

LRESULT App::FontEditorWindow::Window_OnInitMenuPopup(HMENU hMenu, int index, bool isWindowMenu) {
	{
		const MENUITEMINFOW mii{ .cbSize = sizeof mii, .fMask = MIIM_STATE, .fState = static_cast<UINT>(m_bWordWrap ? MFS_CHECKED : 0) };
		SetMenuItemInfoW(hMenu, ID_VIEW_WORDWRAP, FALSE, &mii);
	}
	{
		const MENUITEMINFOW mii{ .cbSize = sizeof mii, .fMask = MIIM_STATE, .fState = static_cast<UINT>(m_bKerning ? MFS_CHECKED : 0) };
		SetMenuItemInfoW(hMenu, ID_VIEW_KERNING, FALSE, &mii);
	}
	{
		const MENUITEMINFOW mii{ .cbSize = sizeof mii, .fMask = MIIM_STATE, .fState = static_cast<UINT>(m_bShowLineMetrics ? MFS_CHECKED : 0) };
		SetMenuItemInfoW(hMenu, ID_VIEW_SHOWLINEMETRICS, FALSE, &mii);
	}
	return 0;
}

LRESULT App::FontEditorWindow::Window_OnMouseMove(uint16_t states, int16_t x, int16_t y) {
	if (FaceElementsListView_OnDragProcessMouseMove(x, y))
		return 0;

	return 0;
}

LRESULT App::FontEditorWindow::Window_OnMouseLButtonUp(uint16_t states, int16_t x, int16_t y) {
	if (FaceElementsListView_OnDragProcessMouseUp(x, y))
		return 0;

	return 0;
}

LRESULT App::FontEditorWindow::Window_OnDestroy() {
	DeleteFont(m_hUiFont);
	PostQuitMessage(0);
	return 0;
}

void App::FontEditorWindow::Window_Redraw() {
	if (!m_pActiveFace)
		return;

	m_bNeedRedraw = true;
	InvalidateRect(m_hWnd, nullptr, FALSE);
}

LRESULT App::FontEditorWindow::Menu_File_New(XivRes::GameFontType fontType) {
	if (Changes_ConfirmIfDirty())
		return 1;

	switch (fontType) {
		case XivRes::GameFontType::font:
			SetCurrentFontSet(Structs::FontSet::NewFromTemplateFont(fontType), "Untitled (font)", true);
			break;
		case XivRes::GameFontType::font_lobby:
			SetCurrentFontSet(Structs::FontSet::NewFromTemplateFont(fontType), "Untitled (font_lobby)", true);
			break;
		case XivRes::GameFontType::chn_axis:
			SetCurrentFontSet(Structs::FontSet::NewFromTemplateFont(fontType), "Untitled (chn_axis)", true);
			break;
		case XivRes::GameFontType::krn_axis:
			SetCurrentFontSet(Structs::FontSet::NewFromTemplateFont(fontType), "Untitled (krn_axis)", true);
			break;
		default:
			SetCurrentFontSet(Structs::FontSet::NewFromTemplateFont(fontType), "Untitled", true);
			break;
	}

	return 0;
}

LRESULT App::FontEditorWindow::Menu_File_Open() {
	using namespace XivRes::FontGenerator;
	static constexpr COMDLG_FILTERSPEC fileTypes[] = {
		{ L"Preset JSON Files (*.json)", L"*.json" },
		{ L"All files (*.*)", L"*" },
	};
	const auto fileTypesSpan = std::span(fileTypes);

	if (Changes_ConfirmIfDirty())
		return 1;

	try {
		IFileOpenDialogPtr pDialog;
		DWORD dwFlags;
		SuccessOrThrow(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
		SuccessOrThrow(pDialog->SetClientGuid(Guid_IFileDialog_Json));
		SuccessOrThrow(pDialog->SetFileTypes(static_cast<UINT>(fileTypesSpan.size()), fileTypesSpan.data()));
		SuccessOrThrow(pDialog->SetFileTypeIndex(0));
		SuccessOrThrow(pDialog->SetTitle(L"Open"));
		SuccessOrThrow(pDialog->GetOptions(&dwFlags));
		SuccessOrThrow(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
		switch (SuccessOrThrow(pDialog->Show(m_hWnd), { HRESULT_FROM_WIN32(ERROR_CANCELLED) })) {
			case HRESULT_FROM_WIN32(ERROR_CANCELLED):
				return 0;
		}

		IShellItemPtr pResult;
		PWSTR pszFileName;
		SuccessOrThrow(pDialog->GetResult(&pResult));
		SuccessOrThrow(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
		if (!pszFileName)
			throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");

		std::unique_ptr<std::remove_pointer<PWSTR>::type, decltype(CoTaskMemFree)*> pszFileNamePtr(pszFileName, CoTaskMemFree);

		SetCurrentFontSet(pszFileName);

	} catch (const std::exception& e) {
		MessageBoxW(m_hWnd, std::format(L"Failed to open file: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
		return 1;
	}

	return 0;
}

LRESULT App::FontEditorWindow::Menu_File_Save() {
	if (m_path.empty() || m_bPathIsNotReal)
		return Menu_File_SaveAs(true);

	try {
		const auto dump = nlohmann::json(m_fontSet).dump(1, '\t');
		std::ofstream(m_path, std::ios::binary).write(&dump[0], dump.size());
		Changes_MarkFresh();
	} catch (const std::exception& e) {
		MessageBoxW(m_hWnd, std::format(L"Failed to save file: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
		return 1;
	}

	return 0;
}

LRESULT App::FontEditorWindow::Menu_File_SaveAs(bool changeCurrentFile) {
	using namespace XivRes::FontGenerator;
	static constexpr COMDLG_FILTERSPEC fileTypes[] = {
		{ L"Preset JSON Files (*.json)", L"*.json" },
		{ L"All files (*.*)", L"*" },
	};
	const auto fileTypesSpan = std::span(fileTypes);

	try {
		const auto dump = nlohmann::json(m_fontSet).dump(1, '\t');

		IFileSaveDialogPtr pDialog;
		DWORD dwFlags;
		SuccessOrThrow(pDialog.CreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER));
		SuccessOrThrow(pDialog->SetClientGuid(Guid_IFileDialog_Json));
		SuccessOrThrow(pDialog->SetFileTypes(static_cast<UINT>(fileTypesSpan.size()), fileTypesSpan.data()));
		SuccessOrThrow(pDialog->SetFileTypeIndex(0));
		SuccessOrThrow(pDialog->SetTitle(L"Save"));
		SuccessOrThrow(pDialog->SetDefaultExtension(L"json"));
		SuccessOrThrow(pDialog->GetOptions(&dwFlags));
		SuccessOrThrow(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
		switch (SuccessOrThrow(pDialog->Show(m_hWnd), { HRESULT_FROM_WIN32(ERROR_CANCELLED) })) {
			case HRESULT_FROM_WIN32(ERROR_CANCELLED):
				return 0;
		}

		IShellItemPtr pResult;
		PWSTR pszFileName;
		SuccessOrThrow(pDialog->GetResult(&pResult));
		SuccessOrThrow(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
		if (!pszFileName)
			throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");

		std::unique_ptr<std::remove_pointer<PWSTR>::type, decltype(CoTaskMemFree)*> pszFileNamePtr(pszFileName, CoTaskMemFree);

		std::ofstream(pszFileName, std::ios::binary).write(&dump[0], dump.size());
		m_path = pszFileName;
		m_bPathIsNotReal = false;
		Changes_MarkFresh();

	} catch (const std::exception& e) {
		MessageBoxW(m_hWnd, std::format(L"Failed to save file: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
		return 1;
	}

	return 0;
}

LRESULT App::FontEditorWindow::Menu_File_Exit() {
	if (Changes_ConfirmIfDirty())
		return 1;

	DestroyWindow(m_hWnd);
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Add() {
	if (!m_pActiveFace)
		return 0;

	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFaceElementsListView, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFaceElementsListView, WM_SETREDRAW, TRUE, 0); });

	std::set<int> indices;
	for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
		indices.insert(i);

	const auto count = ListView_GetItemCount(m_hFaceElementsListView);
	if (indices.empty())
		indices.insert(count);

	ListView_SetItemState(m_hFaceElementsListView, -1, 0, LVIS_SELECTED);

	auto& elements = m_pActiveFace->Elements;
	for (const auto pos : indices | std::views::reverse) {
		auto& element = **elements.emplace(elements.begin() + pos, std::make_unique<Structs::FaceElement>());
		if (pos > 0) {
			element = *elements[static_cast<size_t>(pos) - 1];
			element.WrapModifiers.Codepoints.clear();
		}

		LVITEMW lvi{ .mask = LVIF_PARAM | LVIF_STATE, .iItem = pos, .state = LVIS_SELECTED, .stateMask = LVIS_SELECTED, .lParam = reinterpret_cast<LPARAM>(&element) };
		ListView_InsertItem(m_hFaceElementsListView, &lvi);
		UpdateFaceElementListViewItem(element);
	}

	Changes_MarkDirty();
	m_pActiveFace->OnElementChange();
	Window_Redraw();

	if (indices.size() == 1)
		ShowEditor(*elements[*indices.begin()]);

	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Cut() {
	if (Menu_Edit_Copy())
		return 1;

	Menu_Edit_Delete();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Copy() {
	if (!m_pActiveFace)
		return 1;

	auto objs = nlohmann::json::array();
	for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
		objs.emplace_back(*m_pActiveFace->Elements[i]);

	const auto wstr = XivRes::Unicode::Convert<std::wstring>(objs.dump());

	const auto clipboard = OpenClipboard(m_hWnd);
	if (!clipboard)
		return 1;
	EmptyClipboard();

	bool copied = false;
	HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (wstr.size() + 1) * 2);
	if (hg) {
		if (const auto pLock = GlobalLock(hg)) {
			memcpy(pLock, &wstr[0], (wstr.size() + 1) * 2);
			copied = SetClipboardData(CF_UNICODETEXT, pLock);
		}
		GlobalUnlock(hg);
		if (!copied)
			GlobalFree(hg);
	}
	CloseClipboard();
	return copied ? 0 : 1;
}

LRESULT App::FontEditorWindow::Menu_Edit_Paste() {
	const auto clipboard = OpenClipboard(m_hWnd);
	if (!clipboard)
		return 0;

	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFaceElementsListView, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFaceElementsListView, WM_SETREDRAW, TRUE, 0); });

	std::string data;
	if (const auto pData = GetClipboardData(CF_UNICODETEXT))
		data = XivRes::Unicode::Convert<std::string>(reinterpret_cast<const wchar_t*>(pData));
	CloseClipboard();

	std::vector<Structs::FaceElement> parsedTemplateElements;
	try {
		const auto parsed = nlohmann::json::parse(data);
		if (!parsed.is_array())
			return 0;
		for (const auto& p : parsed) {
			parsedTemplateElements.emplace_back();
			from_json(p, parsedTemplateElements.back());
		}
		if (parsedTemplateElements.empty())
			return 0;
	} catch (const nlohmann::json::exception&) {
		return 0;
	}

	std::set<int> indices;
	for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
		indices.insert(i);

	const auto count = ListView_GetItemCount(m_hFaceElementsListView);
	if (indices.empty())
		indices.insert(count);

	ListView_SetItemState(m_hFaceElementsListView, -1, 0, LVIS_SELECTED);

	auto& elements = m_pActiveFace->Elements;
	for (const auto pos : indices | std::views::reverse) {
		for (const auto& templateElement : parsedTemplateElements | std::views::reverse) {
			auto& element = **elements.emplace(elements.begin() + pos, std::make_unique<Structs::FaceElement>(templateElement));
			LVITEMW lvi{ .mask = LVIF_PARAM | LVIF_STATE, .iItem = pos, .state = LVIS_SELECTED, .stateMask = LVIS_SELECTED, .lParam = reinterpret_cast<LPARAM>(&element) };
			ListView_InsertItem(m_hFaceElementsListView, &lvi);
			UpdateFaceElementListViewItem(element);
		}
	}

	Changes_MarkDirty();
	m_pActiveFace->OnElementChange();
	Window_Redraw();

	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Delete() {
	if (!m_pActiveFace)
		return 0;

	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFaceElementsListView, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFaceElementsListView, WM_SETREDRAW, TRUE, 0); });

	std::set<int> indices;
	for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
		indices.insert(i);
	if (indices.empty())
		return 0;

	for (const auto index : indices | std::views::reverse) {
		ListView_DeleteItem(m_hFaceElementsListView, index);
		m_pActiveFace->Elements.erase(m_pActiveFace->Elements.begin() + index);
	}

	Changes_MarkDirty();
	m_pActiveFace->OnElementChange();
	Window_Redraw();

	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_SelectAll() {
	ListView_SetItemState(m_hFaceElementsListView, -1, LVIS_SELECTED, LVIS_SELECTED);
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_Details() {
	for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
		ShowEditor(*m_pActiveFace->Elements[i]);
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_ChangeParams(int baselineShift, int horizontalOffset, int letterSpacing, float fontSize) {
	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFaceElementsListView, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFaceElementsListView, WM_SETREDRAW, TRUE, 0); });

	auto any = false;
	for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));) {
		any = true;
		auto& e = *m_pActiveFace->Elements[i];
		auto baseChanged = false;
		if (e.Renderer == Structs::RendererEnum::Empty) {
			baseChanged |= !!baselineShift;
			baseChanged |= !!(letterSpacing + horizontalOffset);
			e.RendererSpecific.Empty.Ascent += baselineShift;
			e.RendererSpecific.Empty.LineHeight += letterSpacing + horizontalOffset;
		} else {
			e.WrapModifiers.BaselineShift += baselineShift;
			e.WrapModifiers.HorizontalOffset += horizontalOffset;
			e.WrapModifiers.LetterSpacing += letterSpacing;
		}
		if (fontSize != 0.f) {
			e.Size = std::roundf((e.Size + fontSize) * 10.f) / 10.f;
			baseChanged = true;
		}
		if (baseChanged)
			e.OnFontCreateParametersChange();
		else
			e.OnFontWrappingParametersChange();
		UpdateFaceElementListViewItem(e);
	}
	if (!any)
		return 0;

	Changes_MarkDirty();
	m_pActiveFace->OnElementChange();
	Window_Redraw();

	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_ToggleMergeMode() {
	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFaceElementsListView, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFaceElementsListView, WM_SETREDRAW, TRUE, 0); });

	auto any = false;
	for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));) {
		any = true;
		auto& e = *m_pActiveFace->Elements[i];
		e.MergeMode = static_cast<XivRes::FontGenerator::MergedFontCodepointMode>((static_cast<int>(e.MergeMode) + 1) % static_cast<int>(XivRes::FontGenerator::MergedFontCodepointMode::Enum_Count_));
		e.OnFontWrappingParametersChange();
		UpdateFaceElementListViewItem(e);
	}
	if (!any)
		return 0;

	Changes_MarkDirty();
	m_pActiveFace->OnElementChange();
	Window_Redraw();

	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_MoveUpOrDown(int direction) {
	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFaceElementsListView, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFaceElementsListView, WM_SETREDRAW, TRUE, 0); });

	std::vector<size_t> ids;
	for (auto i = -1; -1 != (i = ListView_GetNextItem(m_hFaceElementsListView, i, LVNI_SELECTED));)
		ids.emplace_back(i);

	if (ids.empty())
		return 0;

	std::vector<size_t> allItems;
	allItems.resize(m_pActiveFace->Elements.size());
	for (auto i = 0; i < allItems.size(); i++)
		allItems[i] = i;

	std::ranges::sort(ids);
	if (direction > 0)
		std::ranges::reverse(ids);

	auto any = false;
	for (const auto& id : ids) {
		if (id + direction < 0 || id + direction >= allItems.size())
			continue;

		any = true;
		std::swap(allItems[id], allItems[id + direction]);
	}
	if (!any)
		return 0;

	std::map<LPARAM, size_t> newLocations;
	for (int i = 0, i_ = static_cast<int>(m_pActiveFace->Elements.size()); i < i_; i++) {
		LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = i };
		ListView_GetItem(m_hFaceElementsListView, &lvi);
		newLocations[lvi.lParam] = allItems[i];
	}

	const auto listViewSortCallback = [](LPARAM lp1, LPARAM lp2, LPARAM ctx) -> int {
		auto& newLocations = *reinterpret_cast<std::map<LPARAM, size_t>*>(ctx);
		const auto nl = newLocations[lp1];
		const auto nr = newLocations[lp2];
		return nl == nr ? 0 : (nl > nr ? 1 : -1);
	};
	ListView_SortItems(m_hFaceElementsListView, listViewSortCallback, &newLocations);

	std::ranges::sort(m_pActiveFace->Elements, [&newLocations](const auto& l, const auto& r) -> bool {
		const auto nl = newLocations[reinterpret_cast<LPARAM>(l.get())];
		const auto nr = newLocations[reinterpret_cast<LPARAM>(r.get())];
		return nl < nr;
	});

	Changes_MarkDirty();
	m_pActiveFace->OnElementChange();
	Window_Redraw();

	return 0;
}

LRESULT App::FontEditorWindow::Menu_Edit_CreateEmptyCopyFromSelection() {
	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFaceElementsListView, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFaceElementsListView, WM_SETREDRAW, TRUE, 0); });

	if (!m_pActiveFace)
		return 0;

	const auto refIndex = ListView_GetNextItem(m_hFaceElementsListView, -1, LVNI_SELECTED);
	if (refIndex == -1)
		return 0;

	ListView_SetItemState(m_hFaceElementsListView, -1, 0, LVIS_SELECTED);

	auto& elements = m_pActiveFace->Elements;
	const auto& ref = *elements[refIndex];
	auto& element = **elements.emplace(elements.begin(), std::make_unique<Structs::FaceElement>());
	element.Size = ref.Size;
	element.RendererSpecific = {
			.Empty = {
				.Ascent = ref.GetWrappedFont()->GetAscent() + ref.WrapModifiers.BaselineShift,
				.LineHeight = ref.GetWrappedFont()->GetLineHeight(),
			},
	};

	LVITEMW lvi{ .mask = LVIF_PARAM | LVIF_STATE, .iItem = 0, .state = LVIS_SELECTED, .stateMask = LVIS_SELECTED, .lParam = reinterpret_cast<LPARAM>(&element) };
	ListView_InsertItem(m_hFaceElementsListView, &lvi);
	UpdateFaceElementListViewItem(element);

	Changes_MarkDirty();
	m_pActiveFace->OnElementChange();
	Window_Redraw();

	return 0;
}

LRESULT App::FontEditorWindow::Menu_View_NextOrPrevFont(int direction) {
	auto i = ListBox_GetCurSel(m_hFacesListBox);
	if (i + direction < 0 || i + direction >= static_cast<int>(m_fontSet.Faces.size()))
		return 0;

	i += direction;
	ListBox_SetCurSel(m_hFacesListBox, i);
	m_pActiveFace = m_fontSet.Faces[i].get();
	UpdateFaceElementList();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_View_WordWrap() {
	m_bWordWrap = !m_bWordWrap;
	Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_View_Kerning() {
	m_bKerning = !m_bKerning;
	Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_View_ShowLineMetrics() {
	m_bShowLineMetrics = !m_bShowLineMetrics;
	Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_View_Zoom(int zoom) {
	m_nZoom = zoom;
	Window_Redraw();
	return 0;
}

LRESULT App::FontEditorWindow::Menu_Export_Preview() {
	using namespace XivRes::FontGenerator;

	try {
		ProgressDialog progressDialog(m_hWnd, "Exporting...");
		ShowWindow(m_hWnd, SW_HIDE);
		const auto hideWhilePacking = XivRes::Internal::CallOnDestruction([this]() { ShowWindow(m_hWnd, SW_SHOW); });
		const auto [fdts, mips] = CompileCurrentFontSet(progressDialog);

		auto texturesAll = std::make_shared<XivRes::TextureStream>(mips[0]->Type, mips[0]->Width, mips[0]->Height, 1, 1, mips.size());
		for (size_t i = 0; i < mips.size(); i++)
			texturesAll->SetMipmap(0, i, mips[i]);

		std::vector<std::pair<std::string, std::shared_ptr<XivRes::FontGenerator::IFixedSizeFont>>> resultFonts;
		for (size_t i = 0; i < fdts.size(); i++)
			resultFonts.emplace_back(m_fontSet.Faces[i]->Name, std::make_shared<XivRes::FontGenerator::GameFontdataFixedSizeFont>(fdts[i], mips, m_fontSet.Faces[i]->Name, ""));

		ExportPreviewWindow::ShowNew(std::move(resultFonts));
		std::thread([texturesAll]() {XivRes::Internal::ShowTextureStream(*texturesAll); }).detach();
		return 0;
	} catch (const ProgressDialog::ProgressDialogCancelledError&) {
		return 1;
	} catch (const std::exception& e) {
		MessageBoxW(m_hWnd, std::format(L"Failed to export: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
		return 1;
	}
}

LRESULT App::FontEditorWindow::Menu_Export_Raw() {
	using namespace XivRes::FontGenerator;

	try {
		IFileOpenDialogPtr pDialog;
		DWORD dwFlags;
		SuccessOrThrow(pDialog.CreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER));
		SuccessOrThrow(pDialog->SetClientGuid(Guid_IFileDialog_Export));
		SuccessOrThrow(pDialog->SetTitle(L"Export raw"));
		SuccessOrThrow(pDialog->GetOptions(&dwFlags));
		SuccessOrThrow(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM | FOS_PICKFOLDERS));
		switch (SuccessOrThrow(pDialog->Show(m_hWnd), { HRESULT_FROM_WIN32(ERROR_CANCELLED) })) {
			case HRESULT_FROM_WIN32(ERROR_CANCELLED):
				return 0;
		}

		IShellItemPtr pResult;
		PWSTR pszFileName;
		SuccessOrThrow(pDialog->GetResult(&pResult));
		SuccessOrThrow(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
		if (!pszFileName)
			throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");

		std::unique_ptr<std::remove_pointer<PWSTR>::type, decltype(CoTaskMemFree)*> pszFileNamePtr(pszFileName, CoTaskMemFree);
		const auto basePath = std::filesystem::path(pszFileName);

		ProgressDialog progressDialog(m_hWnd, "Exporting...");
		ShowWindow(m_hWnd, SW_HIDE);
		const auto hideWhilePacking = XivRes::Internal::CallOnDestruction([this]() { ShowWindow(m_hWnd, SW_SHOW); });
		const auto [fdts, mips] = CompileCurrentFontSet(progressDialog);

		progressDialog.UpdateProgress(std::nanf(""));
		progressDialog.UpdateStatusMessage("Writing to files...");

		std::vector<char> buf(32768);
		XivRes::TextureStream textureOne(mips[0]->Type, mips[0]->Width, mips[0]->Height, 1, 1, 1);

		for (size_t i = 0; i < mips.size(); i++) {
			progressDialog.ThrowIfCancelled();
			textureOne.SetMipmap(0, 0, mips[i]);

			std::ofstream out(basePath / std::format(m_fontSet.TexFilenameFormat, i + 1), std::ios::binary);
			size_t pos = 0;

			for (size_t read, pos = 0; (read = textureOne.ReadStreamPartial(pos, &buf[0], buf.size())); pos += read) {
				progressDialog.ThrowIfCancelled();
				out.write(&buf[0], read);
			}
		}

		for (size_t i = 0; i < fdts.size(); i++) {
			progressDialog.ThrowIfCancelled();
			std::ofstream out(basePath / std::format("{}.fdt", m_fontSet.Faces[i]->Name), std::ios::binary);
			size_t pos = 0;

			for (size_t read, pos = 0; (read = fdts[i]->ReadStreamPartial(pos, &buf[0], buf.size())); pos += read) {
				progressDialog.ThrowIfCancelled();
				out.write(&buf[0], read);
			}
		}

	} catch (const ProgressDialog::ProgressDialogCancelledError&) {
		return 1;
	} catch (const std::exception& e) {
		MessageBoxW(m_hWnd, std::format(L"Failed to export: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
		return 1;
	}

	return 0;
}

LRESULT App::FontEditorWindow::Menu_Export_TTMP(CompressionMode compressionMode) {
	using namespace XivRes::FontGenerator;
	static constexpr COMDLG_FILTERSPEC fileTypes[] = {
		{ L"TTMP file (*.ttmp)", L"*.ttmp" },
		{ L"ZIP file (*.zip)", L"*.zip" },
		{ L"All files (*.*)", L"*" },
	};
	const auto fileTypesSpan = std::span(fileTypes);

	std::wstring tmpPath, finalPath;
	try {
		IFileSaveDialogPtr pDialog;
		DWORD dwFlags;
		SuccessOrThrow(pDialog.CreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER));
		SuccessOrThrow(pDialog->SetClientGuid(Guid_IFileDialog_Export));
		SuccessOrThrow(pDialog->SetFileTypes(static_cast<UINT>(fileTypesSpan.size()), fileTypesSpan.data()));
		SuccessOrThrow(pDialog->SetFileTypeIndex(0));
		SuccessOrThrow(pDialog->SetTitle(L"Save"));
		SuccessOrThrow(pDialog->SetFileName(std::format(L"{}.ttmp", m_path.filename().replace_extension(L"").wstring()).c_str()));
		SuccessOrThrow(pDialog->SetDefaultExtension(L"json"));
		SuccessOrThrow(pDialog->GetOptions(&dwFlags));
		SuccessOrThrow(pDialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM));
		switch (SuccessOrThrow(pDialog->Show(m_hWnd), { HRESULT_FROM_WIN32(ERROR_CANCELLED) })) {
			case HRESULT_FROM_WIN32(ERROR_CANCELLED):
				return 0;
		}

		{
			IShellItemPtr pResult;
			PWSTR pszFileName;
			SuccessOrThrow(pDialog->GetResult(&pResult));
			SuccessOrThrow(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFileName));
			if (!pszFileName)
				throw std::runtime_error("DEBUG: The selected file does not have a filesystem path.");

			finalPath = pszFileName;
			CoTaskMemFree(pszFileName);
		}

		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		tmpPath = std::format(L"{}.{:016X}.tmp", finalPath, li.QuadPart);

		zlib_filefunc64_def ffunc;
		fill_win32_filefunc64W(&ffunc);
		zipFile zf = zipOpen2_64(&tmpPath[0], APPEND_STATUS_CREATE, nullptr, &ffunc);
		if (!zf)
			throw std::runtime_error("Failed to create target file");
		auto zfclose = XivRes::Internal::CallOnDestruction([&zf]() { zipClose(zf, nullptr); });

		ProgressDialog progressDialog(m_hWnd, "Exporting...");
		ShowWindow(m_hWnd, SW_HIDE);
		const auto hideWhilePacking = XivRes::Internal::CallOnDestruction([this]() { ShowWindow(m_hWnd, SW_SHOW); });
		const auto [fdts, mips] = CompileCurrentFontSet(progressDialog);

		progressDialog.UpdateProgress(std::nanf(""));

		std::stringstream ttmpl;
		std::vector<char> ttmpd;

		for (size_t i = 0; i < fdts.size(); i++) {
			progressDialog.ThrowIfCancelled();

			const auto targetFileName = std::format("common/font/{}.fdt", m_fontSet.Faces[i]->Name);
			progressDialog.UpdateStatusMessage(std::format("Packing file: {}", targetFileName));

			XivRes::CompressingPackedFileStream<XivRes::BinaryCompressingPacker> packedStream(targetFileName, fdts[i], compressionMode == CompressionMode::CompressWhilePacking ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);

			const auto pos = ttmpd.size();
			ttmpl << nlohmann::json::object({
				{ "DatFile", "000000" },
				{ "FullPath", targetFileName },
				{ "ModOffset", pos },
				{ "ModSize", packedStream.StreamSize() },
				}) << std::endl;

			ttmpd.resize(pos + packedStream.StreamSize());
			ReadStream(packedStream, 0, std::span(ttmpd).subspan(pos));
		}

		for (size_t i = 0; i < mips.size(); i++) {
			progressDialog.ThrowIfCancelled();

			const auto targetFileName = std::format("common/font/{}", std::format(m_fontSet.TexFilenameFormat, i + 1));
			progressDialog.UpdateStatusMessage(std::format("Packing file: {}", targetFileName));

			const auto& mip = mips[i];
			auto textureOne = std::make_shared<XivRes::TextureStream>(mip->Type, mip->Width, mip->Height, 1, 1, 1);
			textureOne->SetMipmap(0, 0, mip);

			XivRes::CompressingPackedFileStream<XivRes::TextureCompressingPacker> packedStream(targetFileName, std::move(textureOne), compressionMode == CompressionMode::CompressWhilePacking ? Z_BEST_COMPRESSION : Z_NO_COMPRESSION);

			const auto pos = ttmpd.size();
			ttmpl << nlohmann::json::object({
				{ "DatFile", "000000" },
				{ "FullPath", targetFileName },
				{ "ModOffset", pos },
				{ "ModSize", packedStream.StreamSize() },
				}) << std::endl;

			ttmpd.resize(pos + packedStream.StreamSize());
			ReadStream(packedStream, 0, std::span(ttmpd).subspan(pos));
		}

		zip_fileinfo zi{};
		zi.tmz_date.tm_sec = zi.tmz_date.tm_min = zi.tmz_date.tm_hour =
			zi.tmz_date.tm_mday = zi.tmz_date.tm_mon = zi.tmz_date.tm_year = 0;
		zi.dosDate = 0;
		zi.internal_fa = 0;
		zi.external_fa = 0;
		FILETIME ft{}, ftLocal{};
		GetSystemTimeAsFileTime(&ft);
		FileTimeToLocalFileTime(&ft, &ftLocal);
		FileTimeToDosDateTime(&ftLocal, ((LPWORD)&zi.dosDate) + 1, ((LPWORD)&zi.dosDate) + 0);

		progressDialog.UpdateStatusMessage("Writing target file...");
		progressDialog.UpdateProgress(0);

		uint64_t totalWriteSize, written = 0;
		const auto ChunkSize = 256 * 1024;
		{
			const auto ttmpls = ttmpl.str();
			totalWriteSize = ttmpls.size() + ttmpd.size();

			if (const auto err = zipOpenNewFileInZip3_64(zf, "TTMPL.mpl", &zi,
				NULL, 0, NULL, 0, NULL /* comment*/,
				compressionMode == CompressionMode::CompressAfterPacking ? Z_DEFLATED : 0,
				compressionMode == CompressionMode::CompressAfterPacking ? Z_BEST_COMPRESSION : 0,
				0, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
				nullptr, crc32_z(0, reinterpret_cast<const uint8_t*>(&ttmpls[0]), ttmpls.size()), 0))
				throw std::runtime_error(std::format("Failed to create TTMPL.mpl inside zip: {}", err));
			std::unique_ptr<std::remove_pointer_t<decltype(zf)>, decltype(zipCloseFileInZip)*> ziClose(zf, zipCloseFileInZip);

			for (size_t offset = 0; offset < ttmpls.size(); offset += ChunkSize) {
				progressDialog.ThrowIfCancelled();

				const auto writeSize = (std::min<size_t>)(ttmpls.size() - offset, ChunkSize);
				if (const auto err = zipWriteInFileInZip(zf, reinterpret_cast<const uint8_t*>(&ttmpls[offset]), static_cast<uint32_t>(writeSize)))
					throw std::runtime_error(std::format("Failed to write to TTMPL.mpl inside zip: {}", err));

				written += writeSize;
				progressDialog.UpdateProgress(1.f * written / totalWriteSize);
			}
		}
		{
			if (const auto err = zipOpenNewFileInZip3_64(zf, "TTMPD.mpd", &zi,
				NULL, 0, NULL, 0, NULL /* comment*/,
				compressionMode == CompressionMode::CompressAfterPacking ? Z_DEFLATED : 0,
				compressionMode == CompressionMode::CompressAfterPacking ? Z_BEST_COMPRESSION : 0,
				0, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
				nullptr, crc32_z(0, reinterpret_cast<const uint8_t*>(&ttmpd[0]), ttmpd.size()), 0))
				throw std::runtime_error(std::format("Failed to create TTMPD.mpd inside zip: {}", err));
			std::unique_ptr<std::remove_pointer_t<decltype(zf)>, decltype(zipCloseFileInZip)*> ziClose(zf, zipCloseFileInZip);

			for (size_t offset = 0; offset < ttmpd.size(); offset += ChunkSize) {
				progressDialog.ThrowIfCancelled();

				const auto writeSize = (std::min<size_t>)(ttmpd.size() - offset, ChunkSize);
				if (const auto err = zipWriteInFileInZip(zf, reinterpret_cast<const uint8_t*>(&ttmpd[offset]), static_cast<uint32_t>(writeSize)))
					throw std::runtime_error(std::format("Failed to write to TTMPL.mpl inside zip: {}", err));

				written += writeSize;
				progressDialog.UpdateProgress(1.f * written / totalWriteSize);
			}
		}

		zfclose.Clear();

		try {
			std::filesystem::remove(finalPath);
		} catch (...) {
			// ignore
		}
		std::filesystem::rename(tmpPath, finalPath);

	} catch (const ProgressDialog::ProgressDialogCancelledError&) {
		return 1;
	} catch (const std::exception& e) {
		if (!tmpPath.empty()) {
			try {
				std::filesystem::remove(tmpPath);
			} catch (...) {
				// ignore
			}
		}
		MessageBoxW(m_hWnd, std::format(L"Failed to export: {}", XivRes::Unicode::Convert<std::wstring>(e.what())).c_str(), GetWindowString(m_hWnd).c_str(), MB_OK | MB_ICONERROR);
		return 1;
	}

	return 0;
}

LRESULT App::FontEditorWindow::Edit_OnCommand(uint16_t commandId) {
	switch (commandId) {
		case EN_CHANGE:
			if (m_pActiveFace) {
				auto& face = *m_pActiveFace;
				face.PreviewText = XivRes::Unicode::Convert<std::string>(GetWindowString(m_hEdit));
				Changes_MarkDirty();
				Window_Redraw();
			} else
				return -1;
			return 0;
	}

	return 0;
}

LRESULT App::FontEditorWindow::FaceListBox_OnCommand(uint16_t commandId) {
	switch (commandId) {
		case LBN_SELCHANGE:
		{
			const auto iItem = ListBox_GetCurSel(m_hFacesListBox);
			if (iItem != LB_ERR) {
				m_pActiveFace = m_fontSet.Faces[iItem].get();
				UpdateFaceElementList();
			}
			return 0;
		}
	}
	return 0;
}

LRESULT App::FontEditorWindow::FaceElementsListView_OnBeginDrag(NM_LISTVIEW & nmlv) {
	if (!m_pActiveFace)
		return -1;

	m_bIsReorderingFaceElementList = true;
	SetCapture(m_hWnd);
	SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
	return 0;
}

bool App::FontEditorWindow::FaceElementsListView_OnDragProcessMouseUp(int16_t x, int16_t y) {
	if (!m_bIsReorderingFaceElementList)
		return false;

	m_bIsReorderingFaceElementList = false;
	ReleaseCapture();
	FaceElementsListView_DragProcessDragging(x, y);
	return true;
}

bool App::FontEditorWindow::FaceElementsListView_OnDragProcessMouseMove(int16_t x, int16_t y) {
	if (!m_bIsReorderingFaceElementList)
		return false;

	FaceElementsListView_DragProcessDragging(x, y);
	return true;
}

bool App::FontEditorWindow::FaceElementsListView_DragProcessDragging(int16_t x, int16_t y) {
	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFaceElementsListView, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFaceElementsListView, WM_SETREDRAW, TRUE, 0); });

	// Determine the dropped item
	LVHITTESTINFO lvhti{
		.pt = {x, y},
	};
	ClientToScreen(m_hWnd, &lvhti.pt);
	ScreenToClient(m_hFaceElementsListView, &lvhti.pt);
	ListView_HitTest(m_hFaceElementsListView, &lvhti);

	// Out of the ListView?
	if (lvhti.iItem == -1) {
		POINT ptRef{};
		ListView_GetItemPosition(m_hFaceElementsListView, 0, &ptRef);
		if (lvhti.pt.y < ptRef.y)
			lvhti.iItem = 0;
		else {
			RECT rcListView;
			GetClientRect(m_hFaceElementsListView, &rcListView);
			ListView_GetItemPosition(m_hFaceElementsListView, ListView_GetItemCount(m_hFaceElementsListView) - 1, &ptRef);
			if (lvhti.pt.y >= ptRef.y || lvhti.pt.y >= rcListView.bottom - rcListView.top)
				lvhti.iItem = ListView_GetItemCount(m_hFaceElementsListView) - 1;
			else
				return false;
		}
	}

	auto& face = *m_pActiveFace;

	// Rearrange the items
	std::set<int> sourceIndices;
	for (auto iPos = -1; -1 != (iPos = ListView_GetNextItem(m_hFaceElementsListView, iPos, LVNI_SELECTED));)
		sourceIndices.insert(iPos);

	struct SortInfoType {
		std::vector<int> oldIndices;
		std::vector<int> newIndices;
		std::map<LPARAM, int> sourcePtrs;
	} sortInfo;
	sortInfo.oldIndices.reserve(face.Elements.size());
	for (int i = 0, i_ = static_cast<int>(face.Elements.size()); i < i_; i++) {
		LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = i };
		ListView_GetItem(m_hFaceElementsListView, &lvi);
		sortInfo.sourcePtrs[lvi.lParam] = i;
		if (!sourceIndices.contains(i))
			sortInfo.oldIndices.push_back(i);
	}

	{
		int i = (std::max<int>)(0, 1 + lvhti.iItem - static_cast<int>(sourceIndices.size()));
		for (const auto sourceIndex : sourceIndices)
			sortInfo.oldIndices.insert(sortInfo.oldIndices.begin() + i++, sourceIndex);
	}

	sortInfo.newIndices.resize(sortInfo.oldIndices.size());
	auto changed = false;
	for (int i = 0, i_ = static_cast<int>(sortInfo.oldIndices.size()); i < i_; i++) {
		changed |= i != sortInfo.oldIndices[i];
		sortInfo.newIndices[sortInfo.oldIndices[i]] = i;
	}

	if (!changed)
		return false;

	const auto listViewSortCallback = [](LPARAM lp1, LPARAM lp2, LPARAM ctx) -> int {
		auto& sortInfo = *reinterpret_cast<SortInfoType*>(ctx);
		const auto il = sortInfo.sourcePtrs[lp1];
		const auto ir = sortInfo.sourcePtrs[lp2];
		const auto nl = sortInfo.newIndices[il];
		const auto nr = sortInfo.newIndices[ir];
		return nl == nr ? 0 : (nl > nr ? 1 : -1);
	};
	ListView_SortItems(m_hFaceElementsListView, listViewSortCallback, &sortInfo);

	std::ranges::sort(face.Elements, [&sortInfo](const auto& l, const auto& r) -> bool {
		const auto il = sortInfo.sourcePtrs[reinterpret_cast<LPARAM>(l.get())];
		const auto ir = sortInfo.sourcePtrs[reinterpret_cast<LPARAM>(r.get())];
		const auto nl = sortInfo.newIndices[il];
		const auto nr = sortInfo.newIndices[ir];
		return nl < nr;
	});

	Changes_MarkDirty();
	m_pActiveFace->OnElementChange();
	Window_Redraw();

	return true;
}

LRESULT App::FontEditorWindow::FaceElementsListView_OnDblClick(NMITEMACTIVATE & nmia) {
	if (nmia.iItem == -1)
		return 0;
	if (m_pActiveFace == nullptr)
		return 0;
	if (nmia.iItem >= m_pActiveFace->Elements.size())
		return 0;
	ShowEditor(*m_pActiveFace->Elements[nmia.iItem]);
	return 0;
}

void App::FontEditorWindow::SetCurrentFontSet(std::filesystem::path path) {
	const auto s = ReadStreamIntoVector<char>(XivRes::FileStream(path));
	const auto j = nlohmann::json::parse(s.begin(), s.end());
	SetCurrentFontSet(j.get<Structs::FontSet>(), path, false);
}

void App::FontEditorWindow::SetCurrentFontSet(Structs::FontSet fontSet, std::filesystem::path path, bool fakePath) {
	m_fontSet = std::move(fontSet);
	m_path = std::move(path);
	m_bPathIsNotReal = fakePath;

	UpdateFaceList();
	Changes_MarkFresh();
}

void App::FontEditorWindow::Changes_MarkFresh() {
	m_bChanged = false;

	SetWindowTextW(m_hWnd, std::format(
		L"{} - Font Editor",
		m_path.filename().c_str()
	).c_str());
}

void App::FontEditorWindow::Changes_MarkDirty() {
	if (m_bChanged)
		return;

	m_bChanged = true;

	SetWindowTextW(m_hWnd, std::format(
		L"{} - Font Editor*",
		m_path.filename().c_str()
	).c_str());
}

bool App::FontEditorWindow::Changes_ConfirmIfDirty() {
	if (m_bChanged) {
		switch (MessageBoxW(m_hWnd, L"There are unsaved changes. Do you want to save your changes?", GetWindowString(m_hWnd).c_str(), MB_YESNOCANCEL)) {
			case IDYES:
				if (Menu_File_Save())
					return true;
				break;
			case IDNO:
				break;
			case IDCANCEL:
				return true;
		}
	}
	return false;
}

void App::FontEditorWindow::ShowEditor(Structs::FaceElement & element) {
	auto& pEditorWindow = m_editors[&element];
	if (pEditorWindow && pEditorWindow->IsOpened()) {
		pEditorWindow->Activate();
	} else {
		pEditorWindow = std::make_unique<FaceElementEditorDialog>(m_hWnd, element, [this, &element]() {
			UpdateFaceElementListViewItem(element);
			Changes_MarkDirty();
			m_pActiveFace->OnElementChange();
			Window_Redraw();
		});
	}
}

void App::FontEditorWindow::UpdateFaceList() {
	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFacesListBox, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFacesListBox, WM_SETREDRAW, TRUE, 0); });

	Structs::Face* currentTag = nullptr;
	if (int curSel = ListBox_GetCurSel(m_hFacesListBox); curSel != LB_ERR)
		currentTag = reinterpret_cast<Structs::Face*>(ListBox_GetItemData(m_hFacesListBox, curSel));

	ListBox_ResetContent(m_hFacesListBox);
	auto selectionRestored = false;
	for (int i = 0, i_ = static_cast<int>(m_fontSet.Faces.size()); i < i_; i++) {
		auto& face = *m_fontSet.Faces[i];
		ListBox_AddString(m_hFacesListBox, XivRes::Unicode::Convert<std::wstring>(face.Name).c_str());
		ListBox_SetItemData(m_hFacesListBox, i, &face);
		if (currentTag == &face) {
			ListBox_SetCurSel(m_hFacesListBox, i);
			selectionRestored = true;
		}
	}

	if (!selectionRestored) {
		if (m_fontSet.Faces.empty())
			m_pActiveFace = nullptr;
		else {
			ListBox_SetCurSel(m_hFacesListBox, 0);
			m_pActiveFace = m_fontSet.Faces.empty() ? nullptr : m_fontSet.Faces[0].get();
		}

		UpdateFaceElementList();
		Window_Redraw();
	}
}

void App::FontEditorWindow::UpdateFaceElementList() {
	const auto tempDisableRedraw = std::shared_ptr<void>(nullptr, [this, _ = SendMessage(m_hFaceElementsListView, WM_SETREDRAW, FALSE, 0)](void*) { SendMessage(m_hFaceElementsListView, WM_SETREDRAW, TRUE, 0); });

	if (!m_pActiveFace) {
		ListView_DeleteAllItems(m_hFaceElementsListView);
		return;
	}

	std::map<LPARAM, size_t> activeElementTags;
	for (const auto& pElement : m_pActiveFace->Elements) {
		const auto& element = *pElement;
		const auto lp = reinterpret_cast<LPARAM>(&element);
		activeElementTags[lp] = activeElementTags.size();
		if (LVFINDINFOW lvfi{ .flags = LVFI_PARAM, .lParam = lp };
			ListView_FindItem(m_hFaceElementsListView, -1, &lvfi) != -1)
			continue;

		LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = ListView_GetItemCount(m_hFaceElementsListView), .lParam = lp };
		ListView_InsertItem(m_hFaceElementsListView, &lvi);
		UpdateFaceElementListViewItem(element);
	}

	for (int i = 0, i_ = ListView_GetItemCount(m_hFaceElementsListView); i < i_;) {
		LVITEMW lvi{ .mask = LVIF_PARAM, .iItem = i };
		ListView_GetItem(m_hFaceElementsListView, &lvi);
		if (!activeElementTags.contains(lvi.lParam)) {
			i_--;
			ListView_DeleteItem(m_hFaceElementsListView, i);
		} else
			i++;
	}

	const auto listViewSortCallback = [](LPARAM lp1, LPARAM lp2, LPARAM ctx) -> int {
		const auto& activeElementTags = *reinterpret_cast<const std::map<LPARAM, size_t>*>(ctx);
		const auto nl = activeElementTags.at(lp1);
		const auto nr = activeElementTags.at(lp2);
		return nl == nr ? 0 : (nl > nr ? 1 : -1);
	};
	ListView_SortItems(m_hFaceElementsListView, listViewSortCallback, &activeElementTags);

	Edit_SetText(m_hEdit, XivRes::Unicode::Convert<std::wstring>(m_pActiveFace->PreviewText).c_str());
	Window_Redraw();
}

void App::FontEditorWindow::UpdateFaceElementListViewItem(const Structs::FaceElement & element) {
	LVFINDINFOW lvfi{ .flags = LVFI_PARAM, .lParam = reinterpret_cast<LPARAM>(&element) };
	const auto index = ListView_FindItem(m_hFaceElementsListView, -1, &lvfi);
	if (index == -1)
		return;

	std::wstring buf;
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::FamilyName, &(buf = XivRes::Unicode::Convert<std::wstring>(element.GetWrappedFont()->GetFamilyName()))[0]);
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::SubfamilyName, &(buf = XivRes::Unicode::Convert<std::wstring>(element.GetWrappedFont()->GetSubfamilyName()))[0]);
	if (std::fabsf(element.GetWrappedFont()->GetSize() - element.Size) >= 0.01f) {
		ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::Size, &(buf = std::format(L"{:g}px (req. {:g}px)", element.GetWrappedFont()->GetSize(), element.Size))[0]);
	} else {
		ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::Size, &(buf = std::format(L"{:g}px", element.GetWrappedFont()->GetSize()))[0]);
	}
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::LineHeight, &(buf = std::format(L"{}px", element.GetWrappedFont()->GetLineHeight()))[0]);
	if (element.WrapModifiers.BaselineShift && element.Renderer != Structs::RendererEnum::Empty) {
		ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::Ascent, &(buf = std::format(L"{}px({:+}px)", element.GetBaseFont()->GetAscent(), element.WrapModifiers.BaselineShift))[0]);
	} else {
		ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::Ascent, &(buf = std::format(L"{}px", element.GetBaseFont()->GetAscent()))[0]);
	}
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::HorizontalOffset, &(buf = std::format(L"{}px", element.Renderer == Structs::RendererEnum::Empty ? 0 : element.WrapModifiers.HorizontalOffset))[0]);
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::LetterSpacing, &(buf = std::format(L"{}px", element.Renderer == Structs::RendererEnum::Empty ? 0 : element.WrapModifiers.LetterSpacing))[0]);
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::Codepoints, &(buf = element.GetRangeRepresentation())[0]);
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::GlyphCount, &(buf = std::format(L"{}", element.GetWrappedFont()->GetAllCodepoints().size()))[0]);
	switch (element.MergeMode) {
		case XivRes::FontGenerator::MergedFontCodepointMode::AddNew:
			ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::MergeMode, &(buf = L"Add New")[0]);
			break;
		case XivRes::FontGenerator::MergedFontCodepointMode::AddAll:
			ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::MergeMode, &(buf = L"Add All")[0]);
			break;
		case XivRes::FontGenerator::MergedFontCodepointMode::Replace:
			ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::MergeMode, &(buf = L"Replace")[0]);
			break;
		default:
			ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::MergeMode, &(buf = L"Invalid")[0]);
			break;
	}
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::Gamma, &(buf = std::format(L"{:g}", element.Gamma))[0]);
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::Renderer, &(buf = element.GetRendererRepresentation())[0]);
	ListView_SetItemText(m_hFaceElementsListView, index, ListViewCols::Lookup, &(buf = element.GetLookupRepresentation())[0]);
}

std::pair<std::vector<std::shared_ptr<XivRes::FontdataStream>>, std::vector<std::shared_ptr<XivRes::MemoryMipmapStream>>> App::FontEditorWindow::CompileCurrentFontSet(ProgressDialog & progressDialog) {
	progressDialog.UpdateStatusMessage("Loading base fonts...");
	m_fontSet.ConsolidateFonts();

	{
		progressDialog.UpdateStatusMessage("Resolving kerning pairs...");
		XivRes::Internal::ThreadPool<Structs::Face*, size_t> pool;
		for (auto& pFace : m_fontSet.Faces) {
			pool.Submit(pFace.get(), [pFace = pFace.get(), &progressDialog]()->size_t {
				if (progressDialog.IsCancelled())
					return 0;
				return pFace->GetMergedFont()->GetAllKerningPairs().size();
			});
		}
		pool.SubmitDoneAndWait();

		std::vector<std::string> tooManyKernings;
		for (std::optional<std::pair<Structs::Face*, size_t>> res; (res = pool.GetResult());) {
			const auto& [pFace, nKerns] = *res;
			if (nKerns >= 65536)
				tooManyKernings.emplace_back(std::format("\n{}: {}", pFace->Name, nKerns));
		}
		if (!tooManyKernings.empty()) {
			std::ranges::sort(tooManyKernings);
			std::string s = "The number of kerning entries of the following font(s) exceeds the limit of 65535.";
			for (const auto& s2 : tooManyKernings)
				s += s2;
			throw std::runtime_error(s);
		}
	}
	progressDialog.ThrowIfCancelled();

	XivRes::FontGenerator::FontdataPacker packer;
	packer.SetDiscardStep(m_fontSet.DiscardStep);
	packer.SetSideLength(m_fontSet.SideLength);

	for (auto& pFace : m_fontSet.Faces)
		packer.AddFont(pFace->GetMergedFont());

	packer.Compile();

	while (!packer.Wait(std::chrono::milliseconds(200))) {
		progressDialog.ThrowIfCancelled();

		progressDialog.UpdateStatusMessage(packer.GetProgressDescription());
		progressDialog.UpdateProgress(packer.GetProgress());
	}
	if (const auto err = packer.GetErrorIfFailed(); !err.empty())
		throw std::runtime_error(err);

	const auto& fdts = packer.GetTargetFonts();
	const auto& mips = packer.GetMipmapStreams();
	if (mips.empty())
		throw std::runtime_error("No mipmap produced");

	return std::make_pair(fdts, mips);
}

LRESULT WINAPI App::FontEditorWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	return reinterpret_cast<FontEditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))->WndProc(hwnd, msg, wParam, lParam);
}

LRESULT WINAPI App::FontEditorWindow::WndProcInitial(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg != WM_NCCREATE)
		return DefWindowProcW(hwnd, msg, wParam, lParam);

	const auto pCreateStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
	const auto pImpl = reinterpret_cast<FontEditorWindow*>(pCreateStruct->lpCreateParams);
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pImpl));
	SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcStatic));

	return pImpl->WndProc(hwnd, msg, wParam, lParam);
}
