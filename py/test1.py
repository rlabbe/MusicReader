import cv2
import numpy as np
from pathlib import Path
import fitz
import matplotlib.pyplot as plt
from scipy.signal import find_peaks, savgol_filter
from scipy.stats import gaussian_kde

def load_pdf_page(pdf_path: str, page_num: int, dpi: int = 150) -> np.ndarray:
    doc = fitz.open(pdf_path)
    page = doc[page_num]
    pix = page.get_pixmap(matrix=fitz.Matrix(dpi/72, dpi/72))
    img = np.frombuffer(pix.samples, dtype=np.uint8).reshape(pix.height, pix.width, pix.n)
    return cv2.cvtColor(img, cv2.COLOR_RGB2BGR)

def to_grayscale(img: np.ndarray) -> np.ndarray:
    return cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

def gaussian_blur_page(image: np.ndarray, kernel_size: int = 5, sigma: float = 1.0) -> np.ndarray:
    return cv2.GaussianBlur(image, (kernel_size, kernel_size), sigma)

def compute_foreground_threshold(gray: np.ndarray) -> float:
    pixel_values = gray.flatten()
    
    hist, bin_edges = np.histogram(pixel_values, bins=256)
    bin_centers = (bin_edges[:-1] + bin_edges[1:]) / 2
    
    peaks, _ = find_peaks(hist, distance=10)
    
    print(f"Histogram peaks found at indices: {peaks}")
    print(f"Peak values in histogram: {hist[peaks]}")
    
    if len(peaks) >= 2:
        sorted_peaks = sorted(peaks)
        ink_peak_idx = sorted_peaks[0]
        paper_peak_idx = sorted_peaks[-1]
        
        valley_region = hist[ink_peak_idx:paper_peak_idx]
        valley_idx = np.argmin(valley_region) + ink_peak_idx
        threshold = bin_centers[valley_idx]
        
        print(f"Ink peak at index {ink_peak_idx} (value {bin_centers[ink_peak_idx]:.1f})")
        print(f"Paper peak at index {paper_peak_idx} (value {bin_centers[paper_peak_idx]:.1f})")
        print(f"Valley at index {valley_idx} (threshold {threshold:.1f})")
    else:
        threshold = np.median(pixel_values)
        print(f"Only {len(peaks)} peak(s), using median: {threshold:.1f}")
    
    return threshold

def find_staff_gaps(h_projection: np.ndarray, gray_img: np.ndarray, fg_threshold: float, hough_lines, window_length: int = 51) -> tuple:
    smoothed = savgol_filter(h_projection, window_length, 3)
    
    kde = gaussian_kde(smoothed)
    x_range = np.linspace(np.min(smoothed), np.max(smoothed), 1000)
    density = kde(x_range)
    
    local_maxima, _ = find_peaks(density)
    if len(local_maxima) < 2:
        return np.array([]), np.array([])
    
    sorted_by_density = sorted(local_maxima, key=lambda i: -density[i])
    staff_level = x_range[sorted_by_density[0]]
    gap_level = x_range[sorted_by_density[1]]
    
    threshold = (staff_level + gap_level) / 2
    gap_rows = np.where(smoothed > threshold)[0]
    
    if len(gap_rows) == 0:
        return np.array([]), np.array([])
    
    gap_regions = []
    in_gap = False
    gap_start = 0
    
    for i in range(len(gap_rows)):
        if not in_gap:
            gap_start = gap_rows[i]
            in_gap = True
        elif gap_rows[i] > gap_rows[i-1] + 1:
            gap_regions.append((gap_start, gap_rows[i-1]))
            gap_start = gap_rows[i]
    
    if in_gap:
        gap_regions.append((gap_start, gap_rows[-1]))
    
    if len(gap_regions) == 0:
        return np.array([]), np.array([])
    
    gap_widths = np.array([end - start for start, end in gap_regions])
    
    kde_width = gaussian_kde(gap_widths)
    width_range = np.linspace(np.min(gap_widths), np.max(gap_widths), 1000)
    width_density = kde_width(width_range)
    
    width_peaks, _ = find_peaks(width_density)
    
    if len(width_peaks) >= 2:
        sorted_width_peaks = sorted(width_peaks, key=lambda i: -width_density[i])
        regular_gap_level = width_range[sorted_width_peaks[0]]
        outlier_gap_level = width_range[sorted_width_peaks[1]]
        width_threshold = (regular_gap_level + outlier_gap_level) / 2
        keep_below_threshold = regular_gap_level < outlier_gap_level
    else:
        width_threshold = np.median(gap_widths)
        keep_below_threshold = True
    
    candidates = []
    
    for gap_start, gap_end in gap_regions:
        gap_width = gap_end - gap_start
        
        if keep_below_threshold:
            width_ok = gap_width < width_threshold
        else:
            width_ok = gap_width > width_threshold
        
        if not width_ok:
            continue
        
        gap_center = (gap_start + gap_end) // 2
        
        look_above = min(gap_start, 50)
        look_below = min(len(gray_img) - gap_end, 50)
        
        row_above_start = max(0, gap_start - look_above)
        row_below_end = min(len(gray_img), gap_end + look_below + 1)
        
        search_region = gray_img[row_above_start:row_below_end, :]
        
        inked_cols = np.where(np.any(search_region < fg_threshold, axis=0))[0]
        
        print(f"\nGap {gap_center}:")
        print(f"  Gap range: [{gap_start}, {gap_end}] (width={gap_width})")
        print(f"  Search region: [{row_above_start}, {row_below_end}] (look_above={look_above}, look_below={look_below})")
        print(f"  Inked columns (gray<{fg_threshold:.1f}): {len(inked_cols)} cols")
        
        if len(inked_cols) == 0:
            has_structure = False
            med_extent = 0
            print(f"  -> No inked columns, NO structure")
            candidates.append((gap_center, med_extent))
        else:
            vertical_extents = []
            for col in inked_cols:
                col_data = search_region[:, col]
                inked_rows = np.where(col_data < fg_threshold)[0]
                if len(inked_rows) > 0:
                    extent = np.max(inked_rows) - np.min(inked_rows)
                    vertical_extents.append(extent)
            
            if len(vertical_extents) == 0:
                has_structure = False
                print(f"  -> No vertical extents, NO structure")
                med_extent = 0
            else:
                max_extent = np.max(vertical_extents)
                min_extent = np.min(vertical_extents)
                extent_range = max_extent - min_extent
                mean_extent = np.mean(vertical_extents)
                med_extent = np.median(vertical_extents)
                
                print(f"  Vertical extents: min={min_extent}, max={max_extent}, range={extent_range}, mean={mean_extent:.1f}")
                print(f"  Median extent: {med_extent:.1f}, Stdev: {np.std(vertical_extents):.1f}")
                
                has_structure = True
        
        candidates.append((gap_center, med_extent))
    
    if len(candidates) == 0:
        return np.array([]), np.array([])
    
    print(f"\nChecking line crossings for each gap:")
    
    gap_centers_keep = []
    gap_centers_discard = []
    
    for gap_center, med_ext in candidates:
        lines_crossing = 0
        
        if hough_lines is not None:
            for line in hough_lines:
                x1, y1, x2, y2 = line[0]
                
                y_min = min(y1, y2)
                y_max = max(y1, y2)
                
                if y_min <= gap_center <= y_max:
                    lines_crossing += 1
        
        has_line = lines_crossing > 0
        print(f"Gap {gap_center}: lines_crossing={lines_crossing}, keep={not has_line}")
        
        if not has_line:
            gap_centers_keep.append(gap_center)
        else:
            gap_centers_discard.append(gap_center)
    
    return np.array(gap_centers_keep), np.array(gap_centers_discard)

def overlay_projection(img: np.ndarray, gaps_keep: np.ndarray, gaps_discard: np.ndarray) -> np.ndarray:
    result = img.copy()
    
    for gap_row in gaps_discard:
        for x in range(0, result.shape[1], 20):
            cv2.circle(result, (x, gap_row), 2, (0, 0, 255), -1)
        cv2.putText(result, str(gap_row), (10, gap_row + 5), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 255), 1)
    
    for gap_row in gaps_keep:
        for x in range(0, result.shape[1], 20):
            cv2.circle(result, (x, gap_row), 2, (255, 0, 0), -1)
        cv2.putText(result, str(gap_row), (10, gap_row + 5), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (255, 0, 0), 1)
    
    return result

def visualize(img: np.ndarray, title: str = "Image", scale: float = 0.5):
    if scale != 1.0:
        h, w = img.shape[:2]
        new_h, new_w = int(h * scale), int(w * scale)
        img_scaled = cv2.resize(img, (new_w, new_h))
    else:
        img_scaled = img
    cv2.imshow(title, img_scaled)
    screen_width = 3840
    screen_height = 2160
    window_width = img_scaled.shape[1]
    window_height = img_scaled.shape[0]
    x = (screen_width - window_width) // 2
    y = (screen_height - window_height) // 2 - window_height // 2
    cv2.moveWindow(title, max(0, x), max(0, y))

if __name__ == "__main__":
    import os
    os.system('cls')
    plt.close('all')
    cv2.destroyAllWindows()
    
    pdf_paths = [r"C:\smusic\bach\924.pdf",
                 r"C:\smusic\bach\997_facsimile.pdf"]
    pdf_path = pdf_paths[1]
    page_num = 1
    
    img = load_pdf_page(pdf_path, page_num)
    gray = to_grayscale(img)
    blurred = gaussian_blur_page(gray)
    
    fg_threshold = compute_foreground_threshold(gray)
    
    edges = cv2.Canny(gray, 50, 150)
    
    lines = cv2.HoughLinesP(edges, 1, np.pi/180, 50, minLineLength=30, maxLineGap=10)
    
    h_projection = np.sum(blurred, axis=1)
    h_projection_normalized = h_projection / np.max(h_projection)
    
    gaps_keep, gaps_discard = find_staff_gaps(h_projection_normalized, gray, fg_threshold, lines)
    
    result = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    
    if lines is not None:
        for line in lines:
            x1, y1, x2, y2 = line[0]
            cv2.line(result, (x1, y1), (x2, y2), (0, 255, 0), 1)
    
    for gap_row in gaps_discard:
        cv2.line(result, (0, gap_row), (result.shape[1], gap_row), (0, 0, 255), 2)
    
    for gap_row in gaps_keep:
        cv2.line(result, (0, gap_row), (result.shape[1], gap_row), (255, 0, 0), 2)
    
    visualize(result, "Page with Hough Lines and Gaps", scale=0.5)
    
    print(f"Total lines detected: {len(lines) if lines is not None else 0}")
    print(f"Gaps kept: {len(gaps_keep)}, Gaps discarded: {len(gaps_discard)}")
