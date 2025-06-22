#include "document.h"
#include <QGuiApplication>
#include <QScreen>
#include <unordered_set>
#include <qpainter.h>
#include <Windows.h>
#include "logger.h"
#include "fitz_utils.h"
#include "bookmark.h"
#include "document_load_manager.h"
#pragma warning(push,1)
#include <mupdf/pdf.h>
#pragma warning(pop)
#include "document_helpers.h"
#include "exception_logger.h"


Document::Document(std::filesystem::path filename, int dpi, int start_page)
	: filename_(std::move(filename))
	, dpi_(dpi)
	, start_page_(start_page)
	, current_page_(start_page)
{
	initialize_document();
}


Document::~Document()
{
	kill_loading_ = true;

	if (is_temporary()) {
		std::filesystem::remove(filename_);
		return;
	}

	if (!modified_) return;

	std::unique_lock<std::mutex> lock(save_state_mutex_);
	save_cv_.wait(lock, [this]() { return !is_saving_; });

	save();
	being_destroyed_ = true;
}


Page Document::get_page(int page_num, bool is_current) const
{
	SAFE_METHOD;
	TRACE_FUNCTION_MSG("Requesting page {} of {}", page_num, filename_.string());

	if (is_current)
		current_page_ = page_num;

	auto count = page_count();

	if (page_num < 1 || page_num > count) {
		logger::error(std::format("Invalid page number: {} for {}",
								  page_num, filename_.string()));
		if (count == 0)
			return Page(page_num);
		else
			page_num = 1;
	}
	if (is_current) {
		bool need_to_request = false;
		{
			std::lock_guard lock(read_mutex_);
			if (pages_[page_num - 1].is_empty())
				need_to_request = true;
		}
		if (need_to_request)
			prioritize();
	}

	return pages_[page_num - 1];
}


void Document::prioritize() const
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	if (auto *manager = DocumentLoadManager::instance())
		manager->prioritize_page(filename_);
}


void Document::initialize_document()
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	auto [ctx, doc] = open_fitz(filename_.string());
	if (!ctx || !doc) {
		logger::error("Failed to open document: {}", filename_.string());
		return;
	}

	int total_pages = 0;
	fz_outline *outline = nullptr;

	fz_try(ctx)
	{
		total_pages = fz_count_pages(ctx, doc);
		outline = fz_load_outline(ctx, doc);
		if (outline)
			bookmarks_ = convert_outline_to_bookmarks(outline);
		annotations_ = load_annotations_from_pdf(ctx, doc);
	}
	fz_catch(ctx)
	{
		logger::error("MuPDF exception while loading bookmarks{}: {}", filename_.string(), std::string(fz_caught_message(ctx)));
		close_fitz(ctx, doc);
		return;
	}

	if (total_pages == 0) {
		logger::debug("Document has no pages: {}", filename_.string());
		close_fitz(ctx, doc);
		return;
	}

	pages_.resize(total_pages);
	for (int i = 0; i < total_pages; ++i)
		pages_[i].page_num = i + 1;

	page_info_.resize(total_pages);
	for (int i = 0; i < total_pages; ++i) {
		fz_page *page = nullptr;
		fz_try(ctx)
		{
			page = fz_load_page(ctx, doc, i);
			fz_rect bounds = fz_bound_page(ctx, page);
			page_info_[i] = { i + 1, bounds.x1 - bounds.x0, bounds.y1 - bounds.y0 };
			fz_drop_page(ctx, page);
		}
		fz_catch(ctx)
		{
			if (page) fz_drop_page(ctx, page);
			page_info_[i] = { i + 1, 612.0f, 792.0f }; // Default letter size
		}
	}

	close_fitz(ctx, doc);
	emit bookmarks_loaded();
}


std::vector<int> get_page_load_order(int start_page, int total_pages)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::vector<int> load_order;
	load_order.push_back(start_page);

	// Step 1: Load next page first
	if (start_page + 1 <= total_pages)
		load_order.push_back(start_page + 1);

	// Step 2: Load previous page if it exists
	if (start_page > 0)
		load_order.push_back(start_page - 1);

	// Step 3: Load remaining pages forward (start from +2 to avoid duplicate)
	for (int i = start_page + 2; i <= total_pages; ++i)
		load_order.push_back(i);

	// Step 4: Load remaining pages backward (start from -2 to avoid duplicate)
	for (int i = start_page - 2; i > 0; --i)
		load_order.push_back(i);

	return load_order;
}


std::vector<int> Document::get_pending_pages() const
{
	std::lock_guard<std::mutex> lock(read_mutex_);

	// Get all pending pages
	std::vector<int> pending;
	for (int i = 0; i < page_count(); ++i) {
		if (pages_[i].is_empty())
			pending.push_back(i + 1);
	}

	if (pending.empty()) return pending;

	// Convert to set for fast lookups
	std::unordered_set<int> pending_set(pending.begin(), pending.end());

	// Get load order starting from start_page
	auto load_order = get_page_load_order(current_page_, page_count());

	// Return pending pages in load order
	std::vector<int> ordered_pending;
	for (int page : load_order) {
		if (pending_set.contains(page))
			ordered_pending.push_back(page);
	}

	// Sort and check for duplicates
	std::vector<int> sorted_result = ordered_pending;
	std::sort(sorted_result.begin(), sorted_result.end());

	// Check for duplicates - set breakpoint here
	for (size_t i = 1; i < sorted_result.size(); ++i) {
		if (sorted_result[i] == sorted_result[i - 1]) {
			// Duplicate found - breakpoint here
			int duplicate_page = sorted_result[i];
			(void)duplicate_page; // Prevent unused variable warning
		}
	}

	return ordered_pending;
}


void Document::load_page(int page_num)
{
	SAFE_METHOD;
	TRACE_FUNCTION_MSG("file: {} page: {}", filename_.string(), page_num);

	if (kill_loading_ || page_num < 1 || page_num > page_count())
		return;

	{
		std::lock_guard lock(read_mutex_);
		if (!pages_[page_num - 1].is_empty())
			return;
	}

	auto [ctx, doc] = open_fitz(filename_.string());
	if (!ctx || !doc)
		return;

	QImage img;
	fz_try(ctx)
	{
		img = render_page(ctx, doc, page_num - 1, dpi_, kill_loading_);
	}
	fz_catch(ctx)
	{
		logger::error("MuPDF exception loading page {}: {}", page_num, fz_caught_message(ctx));
		close_fitz(ctx, doc);
		return;
	}

	close_fitz(ctx, doc);

	if (kill_loading_) return;

	//std::this_thread::sleep_for(std::chrono::milliseconds(15000));

	{
		std::lock_guard lock(read_mutex_);
		pages_[page_num - 1] = Page(img, page_num, false);
	}

	emit page_loaded(filename_.string(), page_num);
}


std::pair<float, float> Document::get_page_dimensions_points(int page_num) const
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	if (page_num < 1 || page_num > static_cast<int>(page_info_.size()))
		return { 0.0f, 0.0f };

	const auto &info = page_info_[page_num - 1];
	return { info.width_points, info.height_points };
}


Bookmark *Document::find_bookmark(const BookmarkHandle &handle)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	for (auto &bookmark : bookmarks_) {
		if (bookmark.handle_ == handle) return &bookmark;
		auto child = bookmark.find(handle);
		if (child) return child;
	}
	return nullptr;
}


bool Document::reparent_bookmark(const BookmarkHandle &handle, const BookmarkHandle &new_parent_handle)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	auto *bookmark = find_bookmark(handle);
	if (!bookmark) return false;
	return reparent_bookmark(*bookmark, new_parent_handle, false);
}


bool Document::reparent_bookmark(Bookmark bookmark, const BookmarkHandle &new_parent_handle, bool internal_call)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
	if (bookmarks_.empty()) return false;

	if (!internal_call) {
		undo_stack_.push_back(bookmarks_);
	}

	// Remove from current parent if it had one
	if (bookmark.parent_handle_) {
		auto *old_parent = find_bookmark(bookmark.parent_handle_);
		if (old_parent) old_parent->remove_child(bookmark.handle_);
	} else {
		auto it = std::remove_if(bookmarks_.begin(), bookmarks_.end(),
								 [&](const Bookmark &b) { return b.handle_ == bookmark.handle_; });
		bookmarks_.erase(it, bookmarks_.end());
	}

	// Assign to new parent or move to top level
	if (new_parent_handle) {
		auto *new_parent = find_bookmark(new_parent_handle);
		if (!new_parent) return false;
		new_parent->add_child(bookmark);
		bookmark.parent_handle_ = new_parent_handle;
		std::sort(new_parent->children_.begin(), new_parent->children_.end(), bookmark_sort);
	} else {
		bookmark.parent_handle_.clear();
		bookmarks_.push_back(std::move(bookmark));
		std::sort(bookmarks_.begin(), bookmarks_.end(), bookmark_sort);
	}
	modified_ = true;
	return true;
}


bool Document::indent_bookmark(const BookmarkHandle &handle)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
	if (bookmarks_.empty()) return false;

	undo_stack_.push_back(bookmarks_);

	auto bookmark = find_bookmark(handle);
	if (!bookmark) return false;

	// make a copy before we start deleting things!
	Bookmark bookmark_copy = *bookmark;

	assert(handle == bookmark->handle_);

	// If the bookmark is already top-level, find its previous sibling
	if (!bookmark->parent_handle_) {
		auto it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
							   [&](const Bookmark &b) { return b.handle_ == handle; });
		if (it == bookmarks_.begin()) return false;  // Cannot indent first item (no previous sibling)

		auto new_parent = std::prev(it);  // Move under previous sibling
		bookmarks_.erase(it);              // Remove from top-level list before reparenting
		return reparent_bookmark(bookmark_copy, new_parent->handle_, true);
	} else {
		// Find the current parent and locate the previous sibling within that parent
		auto parent = find_bookmark(bookmark->parent_handle_);
		if (!parent) return false;  // Parent not found (shouldn't happen)

		auto it = std::find_if(parent->children_.begin(), parent->children_.end(),
							   [&](const Bookmark &b) { return b.handle_ == handle; });
		if (it == parent->children_.begin()) return false;  // Cannot indent first child (no previous sibling)

		auto new_parent = std::prev(it);   // Move under previous sibling
		parent->children_.erase(it);       // Remove from old parent before reparenting
		return reparent_bookmark(bookmark_copy, new_parent->handle_, true);
	}
	return false;
}


bool Document::unindent_bookmark(const BookmarkHandle &handle)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
	if (bookmarks_.empty()) return false;

	auto bookmark_ptr = find_bookmark(handle);
	if (!bookmark_ptr) return false;

	// make a copy before we start deleting things!
	Bookmark bookmark = *bookmark_ptr;

	if (!bookmark.parent_handle_) return false;  // Already top-level, can't unindent

	undo_stack_.push_back(bookmarks_);

	auto parent = find_bookmark(bookmark.parent_handle_);
	if (!parent) return false;

	// Remove the bookmark from its current parent
	if (!parent->remove_child(handle)) return false;

	// Unindent means: become a sibling of your current parent
	// So new parent is your current parent's parent
	BookmarkHandle new_parent_handle = parent->parent_handle_;
	bookmark.parent_handle_ = new_parent_handle;

	if (new_parent_handle) {
		// Insert as child of grandparent, right after current parent
		auto *grandparent = find_bookmark(new_parent_handle);
		if (!grandparent) return false;

		auto parent_it = std::find_if(grandparent->children_.begin(),
									 grandparent->children_.end(),
									 [&](const Bookmark &b) { return b.handle_ == parent->handle_; });

		if (parent_it != grandparent->children_.end())
			grandparent->children_.insert(parent_it + 1, bookmark);
		else
			grandparent->children_.push_back(bookmark);
	} else {
		// Parent was top-level, so insert at top level right after parent
		auto parent_it = std::find_if(bookmarks_.begin(), bookmarks_.end(),
									 [&](const Bookmark &b) { return b.handle_ == parent->handle_; });

		if (parent_it != bookmarks_.end())
			bookmarks_.insert(parent_it + 1, bookmark);
		else
			bookmarks_.push_back(bookmark);

	}

	modified_ = true;
	return true;
}

bool Document::rename_bookmark(const BookmarkHandle &handle, const std::string &title)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
	if (bookmarks_.empty()) return false;

	auto bookmark = find_bookmark(handle);
	if (bookmark && bookmark->title_ != title) {
		undo_stack_.push_back(bookmarks_);
		bookmark->title_ = title;
		modified_ = true;
		return true;
	}
	return false;
}


bool Document::remove_bookmark(const BookmarkHandle &handle)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
	if (bookmarks_.empty()) return false;

	for (auto it = bookmarks_.begin(); it != bookmarks_.end(); ++it) {
		if (it->handle_ == handle) {
			undo_stack_.push_back(bookmarks_);
			bookmarks_.erase(it);
			modified_ = true;
			return true;
		}
		if (it->remove_child(handle)) {
			undo_stack_.push_back(bookmarks_);
			modified_ = true;
			return true;
		}
	}
	return false;
}


std::pair<BookmarkHandle, bool> Document::add_bookmark(const std::string &title, int page_num)
{
	return add_bookmark(title, page_num, BookmarkHandle());
}


std::pair<BookmarkHandle, bool> Document::add_bookmark(const std::string &title,
													   int page_num,
													   const BookmarkHandle &parent_handle)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	undo_stack_.push_back(bookmarks_);

	Bookmark new_bookmark(title, page_num);

	if (parent_handle) {
		// If explicit parent specified, just add there
		new_bookmark.parent_handle_ = parent_handle;
		auto parent = find_bookmark(parent_handle);
		if (parent) {
			auto insert_pos = std::upper_bound(parent->children_.begin(), parent->children_.end(), new_bookmark, bookmark_sort);
			parent->children_.insert(insert_pos, new_bookmark);
		}
	} else {
		// Find deepest appropriate parent in hierarchy
		BookmarkHandle best_parent = find_deepest_parent_for_page(page_num, bookmarks_);

		if (best_parent) {
			new_bookmark.parent_handle_ = best_parent;
			auto parent = find_bookmark(best_parent);
			auto insert_pos = std::upper_bound(parent->children_.begin(), parent->children_.end(), new_bookmark, bookmark_sort);
			parent->children_.insert(insert_pos, new_bookmark);
		} else {
			// Add to top level
			auto insert_pos = std::upper_bound(bookmarks_.begin(), bookmarks_.end(), new_bookmark, bookmark_sort);
			bookmarks_.insert(insert_pos, new_bookmark);
		}
	}

	modified_ = true;
	return { new_bookmark.handle_, true };
}



BookmarkHandle Document::find_deepest_parent_for_page(int page_num, const std::vector<Bookmark> &bookmarks)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	// Flatten all bookmarks into page order with their parents
	std::vector<std::pair<int, BookmarkHandle>> flattened;
	flatten_bookmarks(bookmarks, BookmarkHandle(), flattened);

	// Find where page_num fits in the sequence
	for (const auto &[page, parent] : flattened) {
		if (page >= page_num) {
			// Found first bookmark with page >= page_num
			// New bookmark should have same parent
			return parent;
		}
	}

	// Page number is higher than all existing bookmarks
	// Use same parent as last bookmark, or top level if empty
	if (!flattened.empty()) {
		return flattened.back().second;
	}

	return BookmarkHandle(); // Top level
}




bool Document::save()
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	// Don't allow save if we're being destroyed or not modified
	if (being_destroyed_ || !modified_) return false;

	{
		std::lock_guard<std::mutex> lock(save_state_mutex_);
		if (is_saving_ || !modified_)
			return false;

		is_saving_ = true;
	}

	std::vector<Bookmark> bookmarks_copy;
	std::vector<Annotation> annotations_copy;
	{
		std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
		bookmarks_copy = bookmarks_;
		annotations_copy = annotations_;
		modified_ = false;
	}

	BookmarkResult bookmark_result = add_bookmarks_to_pdf(filename_.string(), bookmarks_copy);
	bool success = (bookmark_result == BookmarkResult::Success);

	if (success) {
		success = save_annotations_to_pdf();
		if (!success) {
			logger::error("Failed to save annotations to {}", filename_.string());
		}
	} else {
		logger::error("Failed to save bookmarks to {}: error code {}",
					 filename_.string(), static_cast<int>(bookmark_result));
	}

	if (!success) {
		// Restore modified state if save failed
		std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
		modified_ = true;
	}

	{
		std::lock_guard<std::mutex> lock(save_state_mutex_);
		is_saving_ = false;
	}
	save_cv_.notify_all();

	return success;
}

void Document::clear_completed_features()
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

	// Remove completed futures
	save_futures_.erase(std::remove_if(save_futures_.begin(), save_futures_.end(),
									   [](std::future<void> &f) {
		return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}),
		save_futures_.end());
}


bool Document::can_undo() const
{
	return !undo_stack_.empty();
}


bool Document::can_redo() const
{
	return !redo_stack_.empty();
}


void Document::undo()
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

	if (undo_stack_.empty()) return;

	redo_stack_.push_back(bookmarks_);
	bookmarks_ = std::move(undo_stack_.back());
	undo_stack_.pop_back();

	modified_ = true;
}


void Document::redo()
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

	if (redo_stack_.empty()) return;

	undo_stack_.push_back(bookmarks_);
	bookmarks_ = std::move(redo_stack_.back());
	redo_stack_.pop_back();

	modified_ = true;
}


Annotation *Document::find_annotation(const AnnotationHandle &handle)
{
	SAFE_METHOD;
	TRACE_FUNCTION;


	for (auto &annotation : annotations_) {
		if (annotation.handle_ == handle) return &annotation;
	}
	return nullptr;
}

bool Document::add_annotation(const Annotation &annotation)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);
	annotations_.push_back(annotation);
	modified_ = true;

	bool save_success = save();
	reload_page(annotation.page_num_);
	return save_success;
}

bool Document::remove_annotation(const AnnotationHandle &handle)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

	auto it = std::remove_if(annotations_.begin(), annotations_.end(),
							[&](const Annotation &a) { return a.handle_ == handle; });
	if (it != annotations_.end()) {
		int page_num = it->page_num_;
		annotations_.erase(it, annotations_.end());
		modified_ = true;
		bool save_success = save();
		reload_page(page_num);
		return save_success;
	}
	return false;
}

bool Document::edit_text_annotation(const AnnotationHandle &handle, const std::string &new_text)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

	auto *annotation = find_annotation(handle);
	if (annotation) {
		annotation->text_ = new_text;
		modified_ = true;
		bool save_success = save();
		reload_page(annotation->page_num_);
		return save_success;
	}
	return false;
}


bool Document::move_annotation(const AnnotationHandle &handle, float new_x, float new_y)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::lock_guard<std::recursive_mutex> lock(bookmark_mutex_);

	auto *annotation = find_annotation(handle);
	if (annotation) {
		annotation->x_ = new_x;
		annotation->y_ = new_y;
		modified_ = true;
		bool save_success = save();
		reload_page(annotation->page_num_);
		return save_success;
	}
	return false;
}



void Document::reload_page(int page_num)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	if (page_num < 1 || page_num > page_count()) return;

	auto [ctx, doc] = open_fitz(filename_.string());
	if (!ctx || !doc) return;

	QImage img;
	fz_try(ctx)
	{
		img = render_page(ctx, doc, page_num - 1, dpi_, kill_loading_);
	}
	fz_catch(ctx)
	{
		logger::error("MuPDF exception reloading page {}: {}", page_num, fz_caught_message(ctx));
		close_fitz(ctx, doc);
		return;
	}

	close_fitz(ctx, doc);

	{
		std::lock_guard lock(read_mutex_);
		pages_[page_num - 1] = Page(img, page_num, false);
	}
	emit page_loaded(filename_.string(), page_num);
}


std::vector<Annotation> Document::load_annotations_from_pdf(fz_context *ctx, fz_document *doc)
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	std::vector<Annotation> annotations;
	if (!ctx || !doc) return annotations;

	pdf_document *pdf = pdf_specifics(ctx, doc);
	if (!pdf) return annotations;

	fz_try(ctx)
	{
		int page_count = fz_count_pages(ctx, doc);

		for (int page_idx = 0; page_idx < page_count; ++page_idx) {
			pdf_obj *page_obj = pdf_lookup_page_obj(ctx, pdf, page_idx);
			if (!page_obj) continue;

			pdf_obj *annots = pdf_dict_get(ctx, page_obj, PDF_NAME(Annots));
			if (!annots) continue;

			int annot_count = pdf_array_len(ctx, annots);
			for (int i = 0; i < annot_count; ++i) {
				pdf_obj *annot = pdf_array_get(ctx, annots, i);
				if (!annot) continue;

				pdf_obj *subtype = pdf_dict_get(ctx, annot, PDF_NAME(Subtype));
				if (pdf_name_eq(ctx, subtype, PDF_NAME(FreeText))) {
					// Extract annotation properties
					pdf_obj *rect = pdf_dict_get(ctx, annot, PDF_NAME(Rect));
					pdf_obj *contents = pdf_dict_get(ctx, annot, PDF_NAME(Contents));

					if (rect && contents) {
						// PDF rect format: [x0, y0, x1, y1] = [left, bottom, right, top]
						float x0 = pdf_array_get_real(ctx, rect, 0);  // left
						float y0 = pdf_array_get_real(ctx, rect, 1);  // bottom  
						float x1 = pdf_array_get_real(ctx, rect, 2);  // right
						float y1 = pdf_array_get_real(ctx, rect, 3);  // top

						float x = x0;                    // left edge
						float y = y1;                    // top edge (for consistency with UI)
						float width = x1 - x0;          // right - left
						float height = y1 - y0;         // top - bottom

						const char *text = pdf_to_text_string(ctx, contents);

						// Extract font, size, and color from default appearance
						std::string font_name = "Consolas";  // default
						float font_size = 12.0f;  // default
						int r = 0, g = 0, b = 0;  // default black

						// Parse DA (Default Appearance) string
						pdf_obj *da = pdf_dict_get(ctx, annot, PDF_NAME(DA));
						if (da) {
							const char *da_str = pdf_to_text_string(ctx, da);
							if (da_str) {
								// Parse DA string format: "/FontName FontSize Tf r g b rg"
								std::string da_string(da_str);

								// Extract font name (starts with /)
								size_t font_start = da_string.find('/');
								if (font_start != std::string::npos) {
									size_t font_end = da_string.find(' ', font_start);
									if (font_end != std::string::npos) {
										font_name = da_string.substr(font_start + 1, font_end - font_start - 1);
									}
								}

								// Extract font size (number before "Tf")
								size_t tf_pos = da_string.find("Tf");
								if (tf_pos != std::string::npos) {
									size_t size_start = da_string.rfind(' ', tf_pos - 1);
									if (size_start != std::string::npos) {
										size_start = da_string.rfind(' ', size_start - 1);
										if (size_start != std::string::npos) {
											std::string size_str = da_string.substr(size_start + 1, tf_pos - size_start - 1);
											font_size = std::stof(size_str);
										}
									}
								}

								// Extract color (three numbers before "rg")
								size_t rg_pos = da_string.find("rg");
								if (rg_pos != std::string::npos) {
									// Find the three color values before "rg"
									std::istringstream iss(da_string.substr(0, rg_pos));
									std::string token;
									std::vector<std::string> tokens;
									while (iss >> token) {
										tokens.push_back(token);
									}
									if (tokens.size() >= 3) {
										float rf = std::stof(tokens[tokens.size() - 3]);
										float gf = std::stof(tokens[tokens.size() - 2]);
										float bf = std::stof(tokens[tokens.size() - 1]);
										r = static_cast<int>(rf * 255);
										g = static_cast<int>(gf * 255);
										b = static_cast<int>(bf * 255);
									}
								}
							}
						}

						if (text && strlen(text) > 0) {
							Annotation annotation(
								std::string(text),
								page_idx + 1,  // Convert to 1-based page
								x,
								y,
								width, height,
								FontInfo(QString::fromStdString(font_name), font_size, QColor(r, g, b))
							);

							/*logger::debug("LOAD: page={}, x={:.3f}, y={:.3f}, w={:.3f}, h={:.3f}, text='{}', font='{}' {}pt, color=({},{},{})",
										  annotation.page_num_, annotation.x_, annotation.y_, annotation.width_, annotation.height_,
										  annotation.text_, annotation.font_info_.family.toStdString(), annotation.font_info_.size,
										  annotation.font_info_.color.red(), annotation.font_info_.color.green(), annotation.font_info_.color.blue());

							logger::debug("RAW RECT: [{:.3f}, {:.3f}, {:.3f}, {:.3f}] -> x={:.3f}, y={:.3f}(top), w={:.3f}, h={:.3f}",
								x0, y0, x1, y1, x, y, width, height);*/
							annotations.push_back(annotation);
						}
					}
				}
			}
		}
	}
	fz_catch(ctx)
	{
		logger::error("Error loading annotations: {}", fz_caught_message(ctx));
	}

	return annotations;
}



bool Document::save_annotations_to_pdf()
{
	SAFE_METHOD;
	TRACE_FUNCTION;

	auto [ctx, doc] = open_fitz(filename_.string());
	if (!ctx || !doc) {
		logger::error("Failed to open document for annotation saving: {}", filename_.string());
		return false;
	}

	pdf_document *pdf = pdf_specifics(ctx, doc);
	if (!pdf) {
		logger::error("Not a PDF document: {}", filename_.string());
		close_fitz(ctx, doc);
		return false;
	}

	bool success = false;
	fz_try(ctx)
	{
		// First, delete all existing FreeText annotations
		[[maybe_unused]] bool deleted_any = delete_all_freetext_annotations(ctx, pdf);

		// Add all our annotations
		for (const auto &annotation : annotations_) {
			pdf_page *page = pdf_load_page(ctx, pdf, annotation.page_num_ - 1);
			if (!page) {
				logger::error("Failed to load page {} for annotation", annotation.page_num_);
				continue;
			}

			// Check page bounds to understand coordinate system
			fz_rect page_bounds = fz_bound_page(ctx, (fz_page *)page);

			// Create new annotation
			pdf_annot *new_annot = pdf_create_annot(ctx, page, PDF_ANNOT_FREE_TEXT);
			if (!new_annot) {
				logger::error("Failed to create annotation on page {}", annotation.page_num_);
				pdf_drop_page(ctx, page);
				continue;
			}

			// Set rectangle - COORDINATE SYSTEM FIX
			// PDF coordinate system: Y=0 at bottom, Y=page_height at top
			// Our annotation.y_ is stored as distance from TOP of page
			// Need to convert to distance from BOTTOM of page
			float page_height = page_bounds.y1 - page_bounds.y0;

			fz_rect rect;
			rect.x0 = annotation.x_;                                          // left
			rect.y0 = page_height - annotation.y_;                           // bottom = page_height - y_from_top  
			rect.x1 = annotation.x_ + annotation.width_;                      // right
			rect.y1 = page_height - annotation.y_ + annotation.height_;      // top = bottom + height

			pdf_set_annot_rect(ctx, new_annot, rect);
			pdf_set_annot_contents(ctx, new_annot, annotation.text_.c_str());

			// Set default appearance
			auto &font = annotation.font_info_;
			float color[3] = {
				font.color.red() / 255.0f,
				font.color.green() / 255.0f,
				font.color.blue() / 255.0f
			};

			pdf_set_annot_default_appearance(ctx, new_annot,
										   font.family.toUtf8().constData(),
										   font.size,
										   3, // RGB color space
										   color);
			pdf_set_annot_quadding(ctx, new_annot, 0); // Left aligned

			// Set border style (no border)
			pdf_set_annot_border(ctx, new_annot, 0.0f);


			// Check what the rect is without update
			fz_rect final_rect = pdf_annot_rect(ctx, new_annot);
			/*logger::debug("Final rect without update: x0={:.3f}, y0={:.3f}, x1={:.3f}, y1={:.3f}",
						 final_rect.x0, final_rect.y0, final_rect.x1, final_rect.y1);

			logger::debug("SAVE: page={}, x={:.1f}, y={:.1f}, w={:.1f}, h={:.1f}, text='{}', font='{}' {:.1f}pt, color=({},{},{})",
						 annotation.page_num_, annotation.x_, annotation.y_, annotation.width_, annotation.height_,
						 annotation.text_, annotation.font_info_.family.toStdString(), annotation.font_info_.size,
						 annotation.font_info_.color.red(), annotation.font_info_.color.green(), annotation.font_info_.color.blue());*/

			pdf_drop_page(ctx, page);
		}

		// Save incrementally
		pdf_write_options opts = pdf_default_write_options;
		opts.do_incremental = 1;
		pdf_save_document(ctx, pdf, filename_.string().c_str(), &opts);
		success = true;
	}
	fz_catch(ctx)
	{
		logger::error("MuPDF exception while saving annotations for {}: {}",
					 filename_.string(), fz_caught_message(ctx));
	}

	close_fitz(ctx, doc);
	return success;
}
