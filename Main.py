import cv2
import numpy as np

video = cv2.VideoCapture("【東方】Bad Apple!! ＰＶ【影絵】.mp4")

def find_largest_rectangle(matrix):
    if len(matrix) == 0 or len(matrix[0]) == 0:
        return 0, None
    rows = len(matrix)
    cols = len(matrix[0])
    heights = [0] * cols
    max_area = 0
    best_coordinates = None
    for r in range(rows):
        for c in range(cols):
            if matrix[r][c] == 255:
                heights[c] += 1
            else:
                heights[c] = 0
        stack = []
        for j in range(cols + 1):
            current_height = heights[j] if j < cols else 0
            while stack and heights[stack[-1]] > current_height:
                idx = stack.pop()
                height = heights[idx]
                left = stack[-1] + 1 if stack else 0
                right = j - 1
                area = height * (right - left + 1)
                if area > max_area:
                    max_area = area
                    best_coordinates = {"top_left": (r - height + 1, left),"bottom_right": (r, right)}
            stack.append(j)
    return max_area, best_coordinates

def get_minimum_rectangle_cover(matrix, min_area_threshold=1):
    scratch_matrix = [row[:] for row in matrix]
    cover_rectangles = []
    while True:
        area, position = find_largest_rectangle(scratch_matrix)
        if position is None or area < min_area_threshold:
            break
        cover_rectangles.append(position)
        top, left = position["top_left"]
        bottom, right = position["bottom_right"]
        for r in range(top, bottom + 1):
            for c in range(left, right + 1):
                scratch_matrix[r][c] = 0
    return cover_rectangles

def filter_rectangles(rectangles, rows, cols):
    rectangles = [dict(rect) for rect in rectangles]
    while len(rectangles) >= 2:
        covered = np.zeros((rows, cols), dtype=np.uint8)
        for rect in rectangles:
            top, left = rect["top_left"]
            bottom, right = rect["bottom_right"]
            covered[top:bottom + 1, left:right + 1] = 1
        integral = cv2.integral(covered)
        merged = False
        for i in range(len(rectangles)):
            if merged:
                break
            a = rectangles[i]
            at, al = a["top_left"]
            ab, ar = a["bottom_right"]
            for j in range(i + 1, len(rectangles)):
                b = rectangles[j]
                bt, bl = b["top_left"]
                bb, br = b["bottom_right"]
                top = min(at, bt)
                left = min(al, bl)
                bottom = max(ab, bb)
                right = max(ar, br)
                covered_area = (integral[bottom + 1, right + 1]- integral[top, right + 1]- integral[bottom + 1, left]+ integral[top, left])
                candidate_area = ((bottom - top + 1)* (right - left + 1))
                if covered_area != candidate_area:
                    continue
                new_rect = {"top_left": (top, left),"bottom_right": (bottom, right)}
                rectangles = [rect for k, rect in enumerate(rectangles) if k != i and k != j]
                rectangles.append(new_rect)
                merged = True
                break
        if not merged:
            break
    return rectangles

num = 0
count=0
while True:
    ret, frame = video.read()
    if not ret:
        break
    height, width = frame.shape[:2]
    x = 8 #Refactor variable
    h = height // x
    w = width // x
    low_res = cv2.resize(frame,(w, h),interpolation=cv2.INTER_AREA)
    gray = cv2.cvtColor(low_res, cv2.COLOR_BGR2GRAY)
    _, thresh = cv2.threshold(gray,25,255,cv2.THRESH_BINARY) #all pixels above 25 becomes 255, change if you want a difference

    rectangles = get_minimum_rectangle_cover(thresh, 1)
    rectangles = filter_rectangles(rectangles,thresh.shape[0],thresh.shape[1])
    d = len(rectangles)
    num = max(d, num)
    print(f"Rect: {d}")

    display_frame = cv2.resize(thresh,(w * x, h * x),interpolation=cv2.INTER_NEAREST)
    display_frame = cv2.cvtColor(display_frame,cv2.COLOR_GRAY2BGR)
    for pos in rectangles:
        top, left = pos["top_left"]
        bottom, right = pos["bottom_right"]
        pt1 = (left * x, top * x)
        pt2 = ((right + 1) * x, (bottom + 1) * x)
        cv2.rectangle(display_frame,pt1,pt2,(0, 255, 0),2)
    cv2.imshow("Bad Apple - Filtered Rectangle Cover",display_frame)
    if cv2.waitKey(30) & 0xFF == 27:
        break

video.release()
cv2.destroyAllWindows()

print(f"Max: {num}")