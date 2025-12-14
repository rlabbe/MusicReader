import cv2
import numpy as np
from pathlib import Path
import fitz
import matplotlib.pyplot as plt
from scipy.signal import find_peaks, savgol_filter
from scipy.stats import gaussian_kde

np.set_printoptions(suppress=True, formatter={'int_kind':lambda x: str(int(x))})

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
    if hough_lines is None or len(hough_lines) == 0:
        return np.array([]), np.array([])
    
    horizontal_lines = []
    vertical_lines = []
    
    page_width = gray_img.shape[1]
    
    for line in hough_lines:
        x1, y1, x2, y2 = line[0]
        dx = abs(x2 - x1)
        dy = abs(y2 - y1)
        length = np.sqrt(dx*dx + dy*dy)
        
        if dy == 0:
            angle = 0
        elif dx == 0:
            angle = 90
        else:
            angle = np.arctan2(dy, dx) * 180 / np.pi
        
        if angle < 20:
            horizontal_lines.append((line[0], length))
        elif angle > 70:
            vertical_lines.append(line[0])
    
    print(f"Horizontal lines found: {len(horizontal_lines)}")
    print(f"Vertical lines found: {len(vertical_lines)}")
    
    if len(horizontal_lines) < 2:
        return np.array([]), np.array([]), (0, 0, gray_img.shape[1], gray_img.shape[0])
    
    line_lengths = np.array([length for _, length in horizontal_lines])
    print(f"Horizontal line lengths: min={np.min(line_lengths):.0f}, max={np.max(line_lengths):.0f}, median={np.median(line_lengths):.0f}")
    
    staff_lines = [line for line, length in horizontal_lines]
    
    print(f"Staff lines (long horizontal lines): {len(staff_lines)}")
    
    staff_y_positions = sorted([int(min(y1, y2)) for x1, y1, x2, y2 in staff_lines])
    print(f"Staff line positions: {len(staff_y_positions)} lines")
    
    if len(staff_y_positions) == 0:
        return np.array([]), np.array([]), (0, 0, gray_img.shape[1], gray_img.shape[0])
    
    gaps = []
    for i in range(1, len(staff_y_positions)):
        gap = staff_y_positions[i] - staff_y_positions[i-1]
        gaps.append(gap)
    
    hist_gaps, bins_gaps = np.histogram(gaps, bins=50)
    peaks_gaps, _ = find_peaks(hist_gaps, distance=2)
    
    if len(peaks_gaps) >= 2:
        sorted_gap_peaks = sorted(peaks_gaps, key=lambda i: -hist_gaps[i])
        small_gap_peak = sorted_gap_peaks[0]
        large_gap_peak = sorted_gap_peaks[1]
        threshold = (bins_gaps[small_gap_peak] + bins_gaps[large_gap_peak]) / 2
    else:
        threshold = np.median(gaps) * 1.5
    
    print(f"Stave detection threshold: {threshold:.1f}")
    
    staves = []
    stave_extents = []
    current_band = [staff_y_positions[0]]
    
    for y in staff_y_positions[1:]:
        if y - current_band[-1] <= threshold:
            current_band.append(y)
        else:
            band_center = int(np.mean(current_band))
            band_min = int(np.min(current_band))
            band_max = int(np.max(current_band))
            staves.append(band_center)
            stave_extents.append((band_min, band_max))
            current_band = [y]
    
    band_center = int(np.mean(current_band))
    band_min = int(np.min(current_band))
    band_max = int(np.max(current_band))
    staves.append(band_center)
    stave_extents.append((band_min, band_max))
    
    print(f"Detected {len(staves)} staves (dense bands): {staves}")
    print(f"Stave extents: {stave_extents}")
    
    x_coords_in_staves = []
    for line in staff_lines:
        x1, y1, x2, y2 = line
        y_top = min(y1, y2)
        for stave_min, stave_max in stave_extents:
            if stave_min <= y_top <= stave_max:
                x_coords_in_staves.extend([int(x1), int(x2)])
                break
    
    border_x_min = int(np.min(x_coords_in_staves))
    border_x_max = int(np.max(x_coords_in_staves))
    
    print(f"X range from staves: {border_x_min} to {border_x_max}")
    
    stave_y_ranges = []
    for stave_idx in range(len(staves)):
        stave_min, stave_max = stave_extents[stave_idx]
        stave_line_ys = []
        for x1, y1, x2, y2 in staff_lines:
            y_top = min(y1, y2)
            if stave_min <= y_top <= stave_max:
                stave_line_ys.extend([y1, y2])
        if stave_line_ys:
            stave_y_ranges.append((int(np.min(stave_line_ys)), int(np.max(stave_line_ys))))
    
    border_y_min = int(stave_y_ranges[0][0])
    border_y_max = int(stave_y_ranges[-1][1])
    
    print(f"Computed border: ({border_x_min}, {border_y_min}) to ({border_x_max}, {border_y_max})")
    
    print("\nStaves and their staff lines:")
    for stave_idx, (stave_center, (stave_y_min, stave_y_max)) in enumerate(zip(staves, stave_extents)):
        stave_lines_in_this = [line for line in staff_lines if stave_y_min <= min(line[1], line[3]) <= stave_y_max]
        stave_x_coords = []
        for line in stave_lines_in_this:
            x1, y1, x2, y2 = line
            stave_x_coords.extend([int(x1), int(x2)])
        if stave_x_coords:
            print(f"  Stave {stave_idx}: center_y={stave_center}, y_range=[{stave_y_min},{stave_y_max}], {len(stave_lines_in_this)} lines, x_range=[{int(np.min(stave_x_coords))},{int(np.max(stave_x_coords))}]")
        else:
            print(f"  Stave {stave_idx}: center_y={stave_center}, y_range=[{stave_y_min},{stave_y_max}], NO LINES")
    
    result = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    
    for stave_idx, (stave_center, (stave_y_min, stave_y_max)) in enumerate(zip(staves, stave_extents)):
        stave_lines_in_this = [line for line in staff_lines if stave_y_min <= min(line[1], line[3]) <= stave_y_max]
        stave_x_coords = []
        for line in stave_lines_in_this:
            x1, y1, x2, y2 = line
            stave_x_coords.extend([int(x1), int(x2)])
        if stave_x_coords:
            x_min = int(np.min(stave_x_coords))
            x_max = int(np.max(stave_x_coords))
            cv2.rectangle(result, (x_min, stave_y_min), (x_max, stave_y_max), (200, 100, 255), 2)
            cv2.putText(result, str(stave_idx), (x_min+5, stave_y_min+20), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (200, 100, 255), 1)
    
    visualize(result, "Detected Staves")
    
    return np.array([]), np.array([]), (border_x_min, border_y_min, border_x_max, border_y_max)
    
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
    
    gap_centers_keep = []
    gap_centers_discard = []
    
    for gap_start, gap_end in gap_regions:
        gap_width = gap_end - gap_start
        
        if keep_below_threshold:
            width_ok = gap_width < width_threshold
        else:
            width_ok = gap_width > width_threshold
        
        if not width_ok:
            continue
        
        gap_center = gap_start + gap_width // 2
        
        staff_above_idx = None
        staff_below_idx = None
        for i, stave_y in enumerate(staves):
            if stave_y < gap_center:
                staff_above_idx = i
            elif stave_y > gap_center:
                staff_below_idx = i
                break
        
        has_connecting_vertical = False
        
        if staff_above_idx is not None and staff_below_idx is not None:
            above_min, above_max = stave_extents[staff_above_idx]
            below_min, below_max = stave_extents[staff_below_idx]
            
            vlines_checked = []
            for vline in vertical_lines:
                x1, y1, x2, y2 = vline
                y_min = min(y1, y2)
                y_max = max(y1, y2)
                x_mid = (x1 + x2) / 2
                
                in_above_stave = y_min < above_max
                crosses_gap = y_min < gap_center < y_max
                in_below_stave = y_max > below_min
                
                if in_above_stave and crosses_gap and in_below_stave:
                    has_connecting_vertical = True
                    vlines_checked.append(f"  MATCH: x={x_mid:.0f}, y=[{y_min},{y_max}]")
                    break
                elif crosses_gap:
                    vlines_checked.append(f"  cross_gap: x={x_mid:.0f}, y=[{y_min},{y_max}], in_above={in_above_stave}, in_below={in_below_stave}")
            
            if vlines_checked:
                print(f"Gap {gap_center}: Checked {len(vlines_checked)} vertical lines:")
                for v in vlines_checked[:3]:
                    print(v)
        
        print(f"Gap {gap_center}: staff_above={staff_above_idx}, staff_below={staff_below_idx}, has_connecting_vertical={has_connecting_vertical}")
        
        if has_connecting_vertical:
            gap_centers_discard.append(gap_center)
        else:
            gap_centers_keep.append(gap_center)
    
    return np.array(gap_centers_keep), np.array(gap_centers_discard), (border_x_min, border_y_min, border_x_max, border_y_max)

def visualize(img: np.ndarray, title: str = "Image", gaps_keep=None, gaps_discard=None, y_range: tuple = None):
    if y_range is not None:
        y_start, y_end = y_range
        img = img[y_start:y_end, :]
        if gaps_keep is not None:
            gaps_keep = [g - y_start for g in gaps_keep if y_start <= g < y_end]
        if gaps_discard is not None:
            gaps_discard = [g - y_start for g in gaps_discard if y_start <= g < y_end]
    
    fig, ax = plt.subplots(figsize=(12, 14))
    ax.imshow(img)
    
    if gaps_discard is not None:
        for gap_row in gaps_discard:
            ax.axhline(y=gap_row, color='red', linewidth=2, alpha=0.7)
            ax.text(10, gap_row - 5, str(int(gap_row)), color='red', fontsize=12, weight='bold')
    
    if gaps_keep is not None:
        for gap_row in gaps_keep:
            ax.axhline(y=gap_row, color='blue', linewidth=2, alpha=0.7)
            ax.text(10, gap_row - 5, str(int(gap_row)), color='blue', fontsize=12, weight='bold')
    
    ax.set_title(title)
    plt.tight_layout()
    
    try:
        import tkinter as tk
        root = tk.Tk()
        screen_width = root.winfo_screenwidth()
        screen_height = root.winfo_screenheight()
        root.destroy()
        
        fig_width = 1200
        fig_height = 1400
        x = max(0, (screen_width - fig_width) // 2)
        y = 0
        
        mng = plt.get_current_fig_manager()
        if hasattr(mng, 'window'):
            mng.window.move(x, y)
    except:
        pass
    
    plt.show()

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
    
    lines = cv2.HoughLinesP(edges, 1, np.pi/180, 50, minLineLength=30, maxLineGap=30)
    
    h_projection = np.sum(blurred, axis=1)
    h_projection_normalized = h_projection / np.max(h_projection)
    
    gaps_keep, gaps_discard, border = find_staff_gaps(h_projection_normalized, gray, fg_threshold, lines)
    border_x_min, border_y_min, border_x_max, border_y_max = border
    
    result = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    
    cv2.rectangle(result, (border_x_min, border_y_min), (border_x_max, border_y_max), (128, 128, 128), 2)
    
    if lines is not None:
        for line in lines:
            x1, y1, x2, y2 = line[0]
            dx = abs(x2 - x1)
            dy = abs(y2 - y1)
            
            if dx == 0:
                angle = 90
            elif dy == 0:
                angle = 0
            else:
                angle = np.arctan2(dy, dx) * 180 / np.pi
            
            if angle < 45:
                color = (0, 255, 0)
            else:
                color = (0, 165, 255)
            
            cv2.line(result, (x1, y1), (x2, y2), color, 1)
    
    visualize(result, "Page with Hough Lines and Gaps", gaps_keep=gaps_keep, gaps_discard=gaps_discard)
    
    print(f"Total lines detected: {len(lines) if lines is not None else 0}")
    print(f"Gaps kept: {len(gaps_keep)}, Gaps discarded: {len(gaps_discard)}")
