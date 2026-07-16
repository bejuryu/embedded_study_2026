import math

def arc_to_bezier(x1, y1, rx, ry, phi, large_arc, sweep, x2, y2):
    # Convert SVG arc to cubic bezier curves.
    # phi is in degrees
    phi_rad = math.radians(phi)
    cos_phi = math.cos(phi_rad)
    sin_phi = math.sin(phi_rad)
    
    # Step 1: Compute (x1', y1')
    dx = (x1 - x2) / 2.0
    dy = (y1 - y2) / 2.0
    x1_prime = cos_phi * dx + sin_phi * dy
    y1_prime = -sin_phi * dx + cos_phi * dy
    
    # Ensure radii are large enough
    rx_sq = rx * rx
    ry_sq = ry * ry
    x1_prime_sq = x1_prime * x1_prime
    y1_prime_sq = y1_prime * y1_prime
    
    # Check if radii need scaling
    radii_check = x1_prime_sq / rx_sq + y1_prime_sq / ry_sq
    if radii_check > 1:
        rx = rx * math.sqrt(radii_check)
        ry = ry * math.sqrt(radii_check)
        rx_sq = rx * rx
        ry_sq = ry * ry
        
    # Step 2: Compute (cx', cy')
    sign = -1 if large_arc == sweep else 1
    num = rx_sq * ry_sq - rx_sq * y1_prime_sq - ry_sq * x1_prime_sq
    den = rx_sq * y1_prime_sq + ry_sq * x1_prime_sq
    if num < 0:
        num = 0
    coef = sign * math.sqrt(num / den) if den != 0 else 0
    cx_prime = coef * ((rx * y1_prime) / ry)
    cy_prime = coef * -((ry * x1_prime) / rx)
    
    # Step 3: Compute (cx, cy) from (cx', cy')
    cx = cos_phi * cx_prime - sin_phi * cy_prime + (x1 + x2) / 2.0
    cy = sin_phi * cx_prime + cos_phi * cy_prime + (y1 + y2) / 2.0
    
    # Step 4: Compute theta1 and delta_theta
    ux = (x1_prime - cx_prime) / rx
    uy = (y1_prime - cy_prime) / ry
    vx = (-x1_prime - cx_prime) / rx
    vy = (-y1_prime - cy_prime) / ry
    
    # Angle between two vectors
    def angle_vec(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        len_u = math.sqrt(ux*ux + uy*uy)
        len_v = math.sqrt(vx*vx + vy*vy)
        val = dot / (len_u * len_v)
        # Clamp val
        val = max(-1.0, min(1.0, val))
        ang = math.acos(val)
        if ux * vy - uy * vx < 0:
            ang = -ang
        return ang
        
    theta1 = angle_vec(1, 0, ux, uy)
    delta_theta = angle_vec(ux, uy, vx, vy)
    
    if sweep == 0 and delta_theta > 0:
        delta_theta -= 2 * math.pi
    elif sweep == 1 and delta_theta < 0:
        delta_theta += 2 * math.pi
        
    # Split the arc into segments of at most 90 degrees
    num_segments = int(math.ceil(abs(delta_theta) / (math.pi / 2.0)))
    segments = []
    
    for i in range(num_segments):
        t1 = theta1 + i * (delta_theta / num_segments)
        t2 = theta1 + (i + 1) * (delta_theta / num_segments)
        
        # Draw segment from t1 to t2
        alpha = math.sin(t2 - t1) * (math.sqrt(4 + 3 * math.tan((t2 - t1) / 2.0)**2) - 1) / 3.0
        
        # Start point
        sx = cos_phi * rx * math.cos(t1) - sin_phi * ry * math.sin(t1) + cx
        sy = sin_phi * rx * math.cos(t1) + cos_phi * ry * math.sin(t1) + cy
        
        # End point
        ex = cos_phi * rx * math.cos(t2) - sin_phi * ry * math.sin(t2) + cx
        ey = sin_phi * rx * math.cos(t2) + cos_phi * ry * math.sin(t2) + cy
        
        # First control point
        dx_dt = -cos_phi * rx * math.sin(t1) - sin_phi * ry * math.cos(t1)
        dy_dt = -sin_phi * rx * math.sin(t1) + cos_phi * ry * math.cos(t1)
        cp1x = sx + alpha * dx_dt
        cp1y = sy + alpha * dy_dt
        
        # Second control point
        dx_dt2 = -cos_phi * rx * math.sin(t2) - sin_phi * ry * math.cos(t2)
        dy_dt2 = -sin_phi * rx * math.sin(t2) + cos_phi * ry * math.cos(t2)
        cp2x = ex - alpha * dx_dt2
        cp2y = ey - alpha * dy_dt2
        
        segments.append((cp1x, cp1y, cp2x, cp2y, ex, ey))
        
    return segments

def print_bezier_code(name, x1, y1, rx, ry, phi, large_arc, sweep, x2, y2):
    segments = arc_to_bezier(x1, y1, rx, ry, phi, large_arc, sweep, x2, y2)
    print(f"--- Arc: {name} ---")
    print(f"Start: ({x1}, {y1}) -> Arc({rx}, {ry}, {phi}, {large_arc}, {sweep}) -> End: ({x2}, {y2})")
    path_str = ""
    for cp1x, cp1y, cp2x, cp2y, ex, ey in segments:
        path_str += f"C {cp1x:.3f} {cp1y:.3f}, {cp2x:.3f} {cp2y:.3f}, {ex:.3f} {ey:.3f} "
    print("Bezier Path Segment:", path_str.strip())
    print()

# 1. vol_down.svg: M 15.54 8.46, a 5 5 0 0 1 0 7.07
# Since target is x2 = 15.54, y2 = 8.46 + 7.07 = 15.53
print_bezier_code("vol_down", 15.54, 8.46, 5, 5, 0, 0, 1, 15.54, 15.53)

# 2. vol_up.svg: M 19.07 4.93, a 10 10 0 0 1 0 14.14
# Target is x2 = 19.07, y2 = 4.93 + 14.14 = 19.07
print_bezier_code("vol_up (outer)", 19.07, 4.93, 10, 10, 0, 0, 1, 19.07, 19.07)

# 3. wifi.svg: 
# path1: M5 12.55a11 11 0 0 1 14.08 0 => start (5, 12.55), end (19.08, 12.55)
print_bezier_code("wifi (inner)", 5.0, 12.55, 11, 11, 0, 0, 1, 19.08, 12.55)
# path2: M1.42 9a16 16 0 0 1 21.16 0 => start (1.42, 9), end (22.58, 9)
print_bezier_code("wifi (outer)", 1.42, 9.0, 16, 16, 0, 0, 1, 22.58, 9.0)
# path3: M8.53 16.1a6 6 0 0 1 6.95 0 => start (8.53, 16.1), end (15.48, 16.1)
print_bezier_code("wifi (innermost)", 8.53, 16.1, 6, 6, 0, 0, 1, 15.48, 16.1)

# 4. heart.svg:
# M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z
# Let's break down the arcs in heart.svg:
# Arc 1: start (20.84, 4.61), rx=5.5, ry=5.5, sweep=0, large=0, end = (20.84 - 7.78, 4.61) = (13.06, 4.61)
print_bezier_code("heart (arc1)", 20.84, 4.61, 5.5, 5.5, 0, 0, 0, 13.06, 4.61)
# Arc 2: start after L12 5.67l-1.06-1.06 => start (10.94, 4.61), rx=5.5, ry=5.5, sweep=0, large=0, end = (10.94-7.78, 4.61+7.78) = (3.16, 12.39)
print_bezier_code("heart (arc2)", 10.94, 4.61, 5.5, 5.5, 0, 0, 0, 3.16, 12.39)
# Arc 3: start after l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06 => start (20.84, 12.39), rx=5.5, ry=5.5, sweep=0, large=0, end = (20.84, 12.39-7.78) = (20.84, 4.61)
print_bezier_code("heart (arc3)", 20.84, 12.39, 5.5, 5.5, 0, 0, 0, 20.84, 4.61)

# 5. backspace.svg:
# M21 4H8l-7 8 7 8h13a2 2 0 0 0 2-2V6a2 2 0 0 0-2-2z
# Arc 1: after h13 (current = 21, 20), rx=2, ry=2, sweep=0, large=0, end = (21+2, 20-2) = (23, 18)
print_bezier_code("backspace (arc1)", 21, 20, 2, 2, 0, 0, 0, 23, 18)
# Arc 2: after V6 (current = 23, 6), rx=2, ry=2, sweep=0, large=0, end = (23-2, 6-2) = (21, 4)
print_bezier_code("backspace (arc2)", 23, 6, 2, 2, 0, 0, 0, 21, 4)
