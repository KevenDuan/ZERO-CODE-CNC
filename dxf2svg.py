import json
import math
import os
import sys
from collections import Counter

import ezdxf
from ezdxf.addons.drawing import Frontend, RenderContext
from ezdxf.addons.drawing.layout import Page
from ezdxf.addons.drawing.svg import SVGBackend


TOLERANCE = 0.05
CONNECT_TOLERANCE = 0.2
ARC_SEGMENTS = 24


def distance(p1, p2):
    return math.hypot(p1[0] - p2[0], p1[1] - p2[1])


def point_key(point, tol=TOLERANCE):
    return (round(point[0] / tol), round(point[1] / tol))


def dedupe_points(points, tol=TOLERANCE):
    result = []
    for point in points:
        xy = (float(point[0]), float(point[1]))
        if not result or distance(result[-1], xy) > tol:
            result.append(xy)
    return result


def sample_arc_points(center, radius, start_angle, end_angle, clockwise=False, segments=ARC_SEGMENTS):
    start = math.radians(start_angle)
    end = math.radians(end_angle)

    if clockwise:
        if end >= start:
            end -= math.tau
    else:
        if end <= start:
            end += math.tau

    sweep = end - start
    steps = max(8, int(abs(sweep) / math.tau * segments))
    points = []
    for index in range(steps + 1):
        angle = start + sweep * index / steps
        points.append((center[0] + radius * math.cos(angle), center[1] + radius * math.sin(angle)))
    return points


def sample_spline_points(entity, tol=TOLERANCE):
    flatten_distance = max(tol * 0.5, 0.01)
    try:
        points = [(float(point.x), float(point.y)) for point in entity.flattening(flatten_distance, segments=8)]
    except TypeError:
        points = [(float(point.x), float(point.y)) for point in entity.flattening(flatten_distance)]

    if len(points) < 2:
        fit_points = getattr(entity, "fit_points", [])
        points = [(float(point.x), float(point.y)) for point in fit_points]

    if len(points) < 2:
        control_points = getattr(entity, "control_points", [])
        points = [(float(point.x), float(point.y)) for point in control_points]

    return dedupe_points(points)


def sample_segment(segment, reverse=False):
    if segment["type"] == "line":
        points = [segment["start"], segment["end"]]
    elif segment["type"] == "arc":
        points = sample_arc_points(
            segment["center"],
            segment["radius"],
            segment["start_angle"],
            segment["end_angle"],
            clockwise=segment["clockwise"],
        )
        if points:
            points[0] = segment["start"]
            points[-1] = segment["end"]
    else:
        points = list(segment["points"])
    if reverse:
        points.reverse()
    return dedupe_points(points)


def polygon_area(points):
    if len(points) < 3:
        return 0.0
    area = 0.0
    for index, point in enumerate(points):
        nxt = points[(index + 1) % len(points)]
        area += point[0] * nxt[1] - nxt[0] * point[1]
    return area / 2.0


def bbox_from_points(points):
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    min_x = min(xs)
    max_x = max(xs)
    min_y = min(ys)
    max_y = max(ys)
    return {
        "min_x": round(min_x, 4),
        "min_y": round(min_y, 4),
        "max_x": round(max_x, 4),
        "max_y": round(max_y, 4),
        "width": round(max_x - min_x, 4),
        "height": round(max_y - min_y, 4),
    }


def centroid_from_points(points, closed=True):
    if not points:
        return {"x": 0.0, "y": 0.0}

    if not closed or len(points) < 3:
        bbox = bbox_from_points(points)
        return {
            "x": round((bbox["min_x"] + bbox["max_x"]) / 2.0, 4),
            "y": round((bbox["min_y"] + bbox["max_y"]) / 2.0, 4),
        }

    area = 0.0
    cx = 0.0
    cy = 0.0
    count = len(points)

    for index in range(count):
        p1 = points[index]
        p2 = points[(index + 1) % count]
        cross = p1[0] * p2[1] - p2[0] * p1[1]
        area += cross
        cx += (p1[0] + p2[0]) * cross
        cy += (p1[1] + p2[1]) * cross

    area /= 2.0
    if abs(area) < 1e-9:
        bbox = bbox_from_points(points)
        return {
            "x": round((bbox["min_x"] + bbox["max_x"]) / 2.0, 4),
            "y": round((bbox["min_y"] + bbox["max_y"]) / 2.0, 4),
        }

    return {
        "x": round(cx / (6.0 * area), 4),
        "y": round(cy / (6.0 * area), 4),
    }


def point_in_polygon(point, polygon):
    x, y = point
    inside = False
    count = len(polygon)
    for index in range(count):
        x1, y1 = polygon[index]
        x2, y2 = polygon[(index + 1) % count]
        intersects = ((y1 > y) != (y2 > y))
        if intersects:
            at_x = (x2 - x1) * (y - y1) / ((y2 - y1) or 1e-12) + x1
            if x < at_x:
                inside = not inside
    return inside


def safe_layer(entity):
    return getattr(entity.dxf, "layer", "0")


def polyline_points_from_virtual_entities(entity):
    points = []
    for child in entity.virtual_entities():
        child_type = child.dxftype()
        if child_type == "LINE":
            child_points = [
                (float(child.dxf.start.x), float(child.dxf.start.y)),
                (float(child.dxf.end.x), float(child.dxf.end.y)),
            ]
        elif child_type == "ARC":
            child_points = sample_arc_points(
                (float(child.dxf.center.x), float(child.dxf.center.y)),
                float(child.dxf.radius),
                float(child.dxf.start_angle),
                float(child.dxf.end_angle),
                clockwise=False,
            )
        elif child_type == "SPLINE":
            child_points = sample_spline_points(child)
        else:
            continue

        if points and child_points and distance(points[-1], child_points[0]) <= TOLERANCE:
            child_points = child_points[1:]
        points.extend(child_points)

    return dedupe_points(points)


def is_closed_entity(entity):
    try:
        return bool(entity.closed)
    except AttributeError:
        return False


def loop_is_circular(points):
    if len(points) < 8:
        return False, 0.0

    center = centroid_from_points(points, closed=True)
    radii = [distance((center["x"], center["y"]), point) for point in points]
    if not radii:
        return False, 0.0

    avg_radius = sum(radii) / len(radii)
    if avg_radius <= TOLERANCE:
        return False, 0.0

    variance = sum((radius - avg_radius) ** 2 for radius in radii) / len(radii)
    std_dev = math.sqrt(variance)
    bbox = bbox_from_points(points)
    roundness = abs(bbox["width"] - bbox["height"])
    is_circular = std_dev / avg_radius < 0.025 and roundness < max(TOLERANCE * 2.0, avg_radius * 0.08)
    return is_circular, avg_radius


def serialize_point(point):
    return {"x": round(point[0], 4), "y": round(point[1], 4)}


def serialize_points(points, limit=None):
    source_points = points if limit is None else points[:limit]
    serialized = [serialize_point(point) for point in source_points]
    return serialized


def serialize_geometry_segment(segment, reverse=False):
    if segment["type"] == "line":
        start = segment["end"] if reverse else segment["start"]
        end = segment["start"] if reverse else segment["end"]
        return {
            "type": "line",
            "start": serialize_point(start),
            "end": serialize_point(end),
        }

    if segment["type"] == "arc":
        start = segment["end"] if reverse else segment["start"]
        end = segment["start"] if reverse else segment["end"]
        start_angle = segment["end_angle"] if reverse else segment["start_angle"]
        end_angle = segment["start_angle"] if reverse else segment["end_angle"]
        clockwise = (not segment["clockwise"]) if reverse else segment["clockwise"]
        return {
            "type": "arc",
            "start": serialize_point(start),
            "end": serialize_point(end),
            "center": serialize_point(segment["center"]),
            "radius": round(segment["radius"], 4),
            "start_angle": round(start_angle, 4),
            "end_angle": round(end_angle, 4),
            "clockwise": clockwise,
        }

    if segment["type"] == "circle":
        return {
            "type": "circle",
            "center": serialize_point(segment["center"]),
            "radius": round(segment["radius"], 4),
        }

    spline_points = list(reversed(segment["points"])) if reverse else list(segment["points"])
    result = {
        "type": "spline",
        "points": serialize_points(spline_points),
    }
    if "degree" in segment:
        result["degree"] = segment["degree"]
    return result


def append_feature(features, feature_type, layer, points, confidence, notes=None, radius=None, geometry=None):
    bbox = bbox_from_points(points)
    center = centroid_from_points(points)
    feature = {
        "id": f"F{len(features) + 1:03d}",
        "type": feature_type,
        "layer": layer,
        "closed": True,
        "bbox": bbox,
        "center": center,
        "area": round(abs(polygon_area(points)), 4),
        "confidence": confidence,
        "notes": notes or "",
        "points": serialize_points(points),
        "geometry": geometry or [],
    }
    if radius is not None:
        feature["radius"] = round(radius, 4)
        feature["diameter"] = round(radius * 2.0, 4)
    features.append(feature)


def append_open_feature(features, layer, points, notes, geometry=None):
    bbox = bbox_from_points(points)
    start_point = points[0]
    end_point = points[-1]
    closure_gap = round(distance(start_point, end_point), 4)
    feature = {
        "id": f"F{len(features) + 1:03d}",
        "type": "开放轮廓",
        "layer": layer,
        "closed": False,
        "bbox": bbox,
        "center": centroid_from_points(points, closed=False),
        "length": round(sum(distance(points[index], points[index + 1]) for index in range(len(points) - 1)), 4),
        "confidence": "中",
        "notes": f"{notes} 首尾间隙约 {closure_gap:.4f}。",
        "points": serialize_points(points),
        "geometry": geometry or [],
        "start_point": {"x": round(start_point[0], 4), "y": round(start_point[1], 4)},
        "end_point": {"x": round(end_point[0], 4), "y": round(end_point[1], 4)},
        "closure_gap": closure_gap,
    }
    features.append(feature)


def append_entity_debug_warning(warnings, entity_stats, unsupported_stats):
    if not entity_stats:
        return

    summary = ", ".join(f"{entity_type}:{count}" for entity_type, count in sorted(entity_stats.items()))
    warnings.append(f"DXF 实体统计: {summary}")

    if unsupported_stats:
        unsupported_summary = ", ".join(
            f"{entity_type}:{count}" for entity_type, count in sorted(unsupported_stats.items())
        )
        warnings.append(f"存在当前未处理实体: {unsupported_summary}")


def polyline_geometry_from_virtual_entities(entity):
    points = []
    geometry_segments = []

    for child in entity.virtual_entities():
        child_type = child.dxftype()
        if child_type == "LINE":
            segment = {
                "type": "line",
                "start": (float(child.dxf.start.x), float(child.dxf.start.y)),
                "end": (float(child.dxf.end.x), float(child.dxf.end.y)),
            }
        elif child_type == "ARC":
            center = (float(child.dxf.center.x), float(child.dxf.center.y))
            radius = float(child.dxf.radius)
            start_angle = float(child.dxf.start_angle)
            end_angle = float(child.dxf.end_angle)
            segment = {
                "type": "arc",
                "start": (
                    center[0] + radius * math.cos(math.radians(start_angle)),
                    center[1] + radius * math.sin(math.radians(start_angle)),
                ),
                "end": (
                    center[0] + radius * math.cos(math.radians(end_angle)),
                    center[1] + radius * math.sin(math.radians(end_angle)),
                ),
                "center": center,
                "radius": radius,
                "start_angle": start_angle,
                "end_angle": end_angle,
                "clockwise": False,
            }
        elif child_type == "SPLINE":
            spline_points = sample_spline_points(child)
            if len(spline_points) < 2:
                continue
            segment = {
                "type": "spline",
                "start": spline_points[0],
                "end": spline_points[-1],
                "points": spline_points,
                "degree": int(getattr(child.dxf, "degree", 3)),
            }
        else:
            continue

        segment_points = sample_segment(segment)
        if points and segment_points and distance(points[-1], segment_points[0]) <= TOLERANCE:
            segment_points = segment_points[1:]
        points.extend(segment_points)
        geometry_segments.append(serialize_geometry_segment(segment))

    return dedupe_points(points), geometry_segments


def extract_entities(msp):
    loose_segments = []
    closed_loops = []
    open_polylines = []
    entity_stats = Counter()
    unsupported_stats = Counter()

    for entity in msp:
        entity_type = entity.dxftype()
        layer = safe_layer(entity)
        entity_stats[entity_type] += 1

        if entity_type == "CIRCLE":
            center = (float(entity.dxf.center.x), float(entity.dxf.center.y))
            radius = float(entity.dxf.radius)
            points = sample_arc_points(center, radius, 0.0, 360.0, clockwise=False, segments=48)
            closed_loops.append({
                "layer": layer,
                "points": dedupe_points(points),
                "source": "circle",
                "radius": radius,
                "geometry": [serialize_geometry_segment({"type": "circle", "center": center, "radius": radius})],
            })
        elif entity_type == "LINE":
            loose_segments.append({
                "type": "line",
                "layer": layer,
                "start": (float(entity.dxf.start.x), float(entity.dxf.start.y)),
                "end": (float(entity.dxf.end.x), float(entity.dxf.end.y)),
            })
        elif entity_type == "ARC":
            center = (float(entity.dxf.center.x), float(entity.dxf.center.y))
            radius = float(entity.dxf.radius)
            start_angle = float(entity.dxf.start_angle)
            end_angle = float(entity.dxf.end_angle)
            start = (
                center[0] + radius * math.cos(math.radians(start_angle)),
                center[1] + radius * math.sin(math.radians(start_angle)),
            )
            end = (
                center[0] + radius * math.cos(math.radians(end_angle)),
                center[1] + radius * math.sin(math.radians(end_angle)),
            )
            loose_segments.append({
                "type": "arc",
                "layer": layer,
                "start": start,
                "end": end,
                "center": center,
                "radius": radius,
                "start_angle": start_angle,
                "end_angle": end_angle,
                "clockwise": False,
            })
        elif entity_type == "SPLINE":
            points = sample_spline_points(entity)
            if len(points) < 2:
                unsupported_stats[entity_type] += 1
                continue
            spline_segment = {
                "type": "spline",
                "start": points[0],
                "end": points[-1],
                "points": points,
                "degree": int(getattr(entity.dxf, "degree", 3)),
            }
            if is_closed_entity(entity) or distance(points[0], points[-1]) <= TOLERANCE:
                if distance(points[0], points[-1]) > TOLERANCE:
                    points.append(points[0])
                closed_loops.append({
                    "layer": layer,
                    "points": dedupe_points(points),
                    "source": "spline",
                    "radius": None,
                    "geometry": [serialize_geometry_segment(spline_segment)],
                })
            else:
                loose_segments.append({
                    "layer": layer,
                    **spline_segment,
                })
        elif entity_type in ("LWPOLYLINE", "POLYLINE"):
            points, geometry_segments = polyline_geometry_from_virtual_entities(entity)
            if len(points) < 2:
                continue
            if is_closed_entity(entity):
                if distance(points[0], points[-1]) > TOLERANCE:
                    points.append(points[0])
                closed_loops.append({
                    "layer": layer,
                    "points": dedupe_points(points),
                    "source": entity_type.lower(),
                    "radius": None,
                    "geometry": geometry_segments,
                })
            else:
                open_polylines.append({
                    "layer": layer,
                    "points": dedupe_points(points),
                    "source": entity_type.lower(),
                    "geometry": geometry_segments,
                })
        else:
            unsupported_stats[entity_type] += 1

    return loose_segments, closed_loops, open_polylines, entity_stats, unsupported_stats


def normalize_loose_segments(loose_segments, tol=CONNECT_TOLERANCE):
    if not loose_segments:
        return []

    layer_nodes = {}
    endpoint_node = {}

    for index, segment in enumerate(loose_segments):
        layer = segment["layer"]
        nodes = layer_nodes.setdefault(layer, [])
        for side in ("start", "end"):
            point = segment[side]
            matched_node = None
            best_distance = float("inf")
            for node_index, node_point in enumerate(nodes):
                current_distance = distance(point, node_point)
                if current_distance <= tol and current_distance < best_distance:
                    matched_node = node_index
                    best_distance = current_distance
            if matched_node is None:
                matched_node = len(nodes)
                nodes.append(point)
            endpoint_node[(index, side)] = (layer, matched_node)

    normalized = []
    for index, segment in enumerate(loose_segments):
        updated = dict(segment)
        start_node = endpoint_node[(index, "start")]
        end_node = endpoint_node[(index, "end")]
        updated["start_node"] = start_node
        updated["end_node"] = end_node

        snapped_start = layer_nodes[start_node[0]][start_node[1]]
        snapped_end = layer_nodes[end_node[0]][end_node[1]]
        updated["start"] = snapped_start
        updated["end"] = snapped_end

        if updated["type"] == "spline":
            spline_points = list(updated.get("points", []))
            if spline_points:
                spline_points[0] = snapped_start
                spline_points[-1] = snapped_end
                updated["points"] = dedupe_points(spline_points)

        normalized.append(updated)

    return normalized


def direction_vector(points):
    if len(points) < 2:
        return None
    head = points[0]
    for tail in points[1:]:
        dx = tail[0] - head[0]
        dy = tail[1] - head[1]
        length = math.hypot(dx, dy)
        if length > 1e-9:
            return (dx / length, dy / length)
    return None


def segment_type_priority(segment_type):
    if segment_type == "spline":
        return 2
    if segment_type == "arc":
        return 1
    return 0


def choose_next_segment(current_node, pending, adjacency, loose_segments, degrees, prev_direction, debug_candidates=None):
    best = None
    best_score = None

    for candidate in adjacency.get(current_node, []):
        if candidate not in pending:
            continue

        candidate_segment = loose_segments[candidate]
        reverse = candidate_segment["end_node"] == current_node
        other_node = candidate_segment["start_node"] if reverse else candidate_segment["end_node"]
        candidate_info = {
            "segment_index": candidate,
            "type": candidate_segment["type"],
            "reverse": reverse,
            "from_node": [current_node[0], current_node[1]],
            "to_node": [other_node[0], other_node[1]],
        }
        segment_points = sample_segment(candidate_segment, reverse=reverse)
        if len(segment_points) < 2:
            candidate_info["status"] = "ignored_short_segment"
            if debug_candidates is not None:
                debug_candidates.append(candidate_info)
            continue

        next_direction = direction_vector(segment_points)
        if next_direction is None:
            candidate_info["status"] = "ignored_no_direction"
            if debug_candidates is not None:
                debug_candidates.append(candidate_info)
            continue

        other_degree = degrees.get(other_node, 0)
        span = distance(segment_points[0], segment_points[-1])
        type_priority = segment_type_priority(candidate_segment["type"])
        candidate_info["other_degree"] = other_degree
        candidate_info["span"] = round(span, 4)
        candidate_info["direction"] = [round(next_direction[0], 6), round(next_direction[1], 6)]
        candidate_info["type_priority"] = type_priority

        if prev_direction is not None:
            continuity = prev_direction[0] * next_direction[0] + prev_direction[1] * next_direction[1]
            score = (1, continuity, -abs(other_degree - 2), span)
            candidate_info["continuity"] = round(continuity, 6)
        else:
            # 闭环首步没有历史方向时，优先从曲线段起步，避免被长直线抢走起点。
            score = (0, type_priority, -abs(other_degree - 2), -span)
            candidate_info["continuity"] = None

        candidate_info["score"] = [round(value, 6) if isinstance(value, float) else value for value in score]
        candidate_info["status"] = "candidate"
        if debug_candidates is not None:
            debug_candidates.append(candidate_info)

        if best_score is None or score > best_score:
            best_score = score
            best = (candidate, reverse, segment_points, next_direction)

    if debug_candidates is not None and best is not None:
        selected_index = best[0]
        for candidate_info in debug_candidates:
            candidate_info["selected"] = candidate_info.get("segment_index") == selected_index

    return best


def build_loose_chains(loose_segments, capture_debug=False):
    if not loose_segments:
        return ([], []) if capture_debug else []

    loose_segments = normalize_loose_segments(loose_segments)

    adjacency = {}
    for index, segment in enumerate(loose_segments):
        adjacency.setdefault(segment["start_node"], []).append(index)
        adjacency.setdefault(segment["end_node"], []).append(index)

    chains = []
    chain_debug = []
    visited = set()

    for index, segment in enumerate(loose_segments):
        if index in visited:
            continue

        layer = segment["layer"]
        component = set()
        queue = [index]
        while queue:
            current = queue.pop()
            if current in component:
                continue
            component.add(current)
            current_segment = loose_segments[current]
            for node in (current_segment["start_node"], current_segment["end_node"]):
                for neighbor in adjacency.get(node, []):
                    if neighbor not in component:
                        queue.append(neighbor)

        pending = set(component)
        visited.update(component)

        while pending:
            degrees = {}
            for component_index in pending:
                current_segment = loose_segments[component_index]
                for node in (current_segment["start_node"], current_segment["end_node"]):
                    degrees[node] = degrees.get(node, 0) + 1

            degree_one_nodes = [node for node, degree in degrees.items() if degree == 1]
            if degree_one_nodes:
                start_node = degree_one_nodes[0]
            else:
                start_node = loose_segments[next(iter(pending))]["start_node"]

            current_node = start_node
            points = []
            geometry = []
            prev_direction = None
            chain_progress = 0
            step_guard = 0
            max_steps = len(pending) + 1
            chain_trace = {
                "layer": layer,
                "start_node": [start_node[0], start_node[1]],
                "steps": [],
            } if capture_debug else None

            while True:
                step_guard += 1
                if step_guard > max_steps:
                    if chain_trace is not None:
                        chain_trace["stop_reason"] = "step_guard"
                    break

                debug_candidates = [] if capture_debug else None
                selection = choose_next_segment(
                    current_node=current_node,
                    pending=pending,
                    adjacency=adjacency,
                    loose_segments=loose_segments,
                    degrees=degrees,
                    prev_direction=prev_direction,
                    debug_candidates=debug_candidates,
                )

                if selection is None:
                    if chain_trace is not None:
                        chain_trace["steps"].append({
                            "current_node": [current_node[0], current_node[1]],
                            "candidates": debug_candidates,
                            "selected_segment": None,
                        })
                        chain_trace["stop_reason"] = "no_selection"
                    break

                next_index, reverse, segment_points, next_direction = selection
                pending.remove(next_index)
                chain_progress += 1
                from_node = current_node
                geometry_segment = serialize_geometry_segment(loose_segments[next_index], reverse=reverse)
                appended_to_existing = False

                connection_tol = max(TOLERANCE, CONNECT_TOLERANCE)
                if points and distance(points[-1], segment_points[0]) <= connection_tol:
                    segment_points = segment_points[1:]
                    appended_to_existing = True
                elif points:
                    # 保护性分段：不强行连接不连续段，避免出现虚假的跨段直线。
                    points = dedupe_points(points)
                    if len(points) >= 2:
                        chains.append({"layer": layer, "points": points, "geometry": geometry})
                    points = segment_points
                    geometry = [geometry_segment]
                    appended_to_existing = None

                if not points:
                    points = segment_points
                    geometry = [geometry_segment]
                elif appended_to_existing:
                    points.extend(segment_points)
                    geometry.append(geometry_segment)
                prev_direction = next_direction

                if reverse:
                    current_node = loose_segments[next_index]["start_node"]
                else:
                    current_node = loose_segments[next_index]["end_node"]

                if chain_trace is not None:
                    chain_trace["steps"].append({
                        "from_node": [from_node[0], from_node[1]],
                        "to_node": [current_node[0], current_node[1]],
                        "selected_segment": next_index,
                        "selected_type": loose_segments[next_index]["type"],
                        "reverse": reverse,
                        "candidates": debug_candidates,
                    })

                # 一个闭环形成后立即结束当前链，剩余边在下一轮 pending 中单独处理，
                # 避免在分叉节点继续拼接造成异常折返。
                if current_node == start_node and len(points) >= 3:
                    if chain_trace is not None:
                        chain_trace["stop_reason"] = "closed_loop"
                    break

            points = dedupe_points(points)
            if len(points) >= 2:
                chains.append({"layer": layer, "points": points, "geometry": geometry})
                if chain_trace is not None:
                    chain_trace["points_count"] = len(points)
                    chain_debug.append(chain_trace)
                continue

            # 保底策略：如果这一轮没有消费任何边，强制取出一个剩余段单独成链，
            # 防止复杂节点关系导致 pending 永远不减少而卡死。
            if chain_progress == 0 and pending:
                fallback_index = pending.pop()
                fallback_points = sample_segment(loose_segments[fallback_index])
                fallback_points = dedupe_points(fallback_points)
                if len(fallback_points) >= 2:
                    chains.append({
                        "layer": layer,
                        "points": fallback_points,
                        "geometry": [serialize_geometry_segment(loose_segments[fallback_index])],
                    })
                if chain_trace is not None:
                    chain_trace["stop_reason"] = "fallback"
                    chain_trace["points_count"] = len(fallback_points)
                    chain_trace["fallback_segment"] = fallback_index
                    chain_debug.append(chain_trace)

    return (chains, chain_debug) if capture_debug else chains


def recognize_features(dxf_path):
    doc = ezdxf.readfile(dxf_path)
    msp = doc.modelspace()
    loose_segments, closed_loops, open_polylines, entity_stats, unsupported_stats = extract_entities(msp)
    loose_chains, chain_debug = build_loose_chains(loose_segments, capture_debug=True)

    warnings = []
    append_entity_debug_warning(warnings, entity_stats, unsupported_stats)
    closed_candidates = []

    for loop in closed_loops:
        points = dedupe_points(loop["points"])
        if len(points) < 3:
            continue
        if distance(points[0], points[-1]) > TOLERANCE:
            points.append(points[0])
        points = dedupe_points(points[:-1])
        if len(points) >= 3:
            closed_candidates.append({
                "layer": loop["layer"],
                "points": points,
                "source": loop["source"],
                "radius": loop["radius"],
                "geometry": loop.get("geometry", []),
            })

    open_features = []
    for chain in loose_chains + open_polylines:
        chain_points = dedupe_points(chain["points"])
        if len(chain_points) < 2:
            continue
        if distance(chain_points[0], chain_points[-1]) <= TOLERANCE and len(chain_points) >= 3:
            closed_candidates.append({
                "layer": chain["layer"],
                "points": chain_points[:-1] if distance(chain_points[0], chain_points[-1]) <= TOLERANCE else chain_points,
                "source": "loose_chain",
                "radius": None,
                "geometry": chain.get("geometry", []),
            })
        else:
            open_features.append({
                "layer": chain["layer"],
                "points": chain_points,
                "geometry": chain.get("geometry", []),
            })

    loop_records = []
    for index, candidate in enumerate(closed_candidates):
        points = candidate["points"]
        is_circular, loop_radius = loop_is_circular(points)
        area = abs(polygon_area(points))
        center = centroid_from_points(points)
        loop_records.append({
            "index": index,
            "layer": candidate["layer"],
            "points": points,
            "source": candidate["source"],
            "radius": candidate["radius"] or (loop_radius if is_circular else None),
            "is_circular": candidate["source"] == "circle" or is_circular,
            "area": area,
            "center": (center["x"], center["y"]),
            "geometry": candidate.get("geometry", []),
        })

    features = []
    for record in loop_records:
        containing = []
        for other in loop_records:
            if other["index"] == record["index"] or other["area"] <= record["area"]:
                continue
            if point_in_polygon(record["center"], other["points"]):
                containing.append(other)

        if record["is_circular"]:
            if containing:
                feature_type = "圆孔"
                confidence = "高"
                notes = "检测到封闭圆形特征，可作为钻孔/铣孔候选。"
            else:
                feature_type = "外轮廓"
                confidence = "中"
                notes = "检测到独立圆形闭环，当前按外轮廓处理。"
            append_feature(
                features,
                feature_type,
                record["layer"],
                record["points"],
                confidence,
                notes=notes,
                radius=record["radius"],
                geometry=record["geometry"],
            )
        else:
            contain_level = len(containing)
            if contain_level == 0:
                feature_type = "外轮廓"
                confidence = "高"
                notes = "顶层闭合轮廓，可作为零件外形候选。"
            elif contain_level % 2 == 1:
                feature_type = "内轮廓"
                confidence = "高"
                notes = "位于外轮廓内部，可作为型腔/开口轮廓候选。"
            else:
                feature_type = "岛"
                confidence = "中"
                notes = "位于内轮廓内部，可能是保留岛或嵌套外轮廓。"
            append_feature(
                features,
                feature_type,
                record["layer"],
                record["points"],
                confidence,
                notes=notes,
                geometry=record["geometry"],
            )

    for open_feature in open_features:
        append_open_feature(
            features,
            open_feature["layer"],
            open_feature["points"],
            "检测到未闭合图元链，暂不能直接生成完整加工轮廓。",
            geometry=open_feature.get("geometry", []),
        )
        warnings.append(f"图层 {open_feature['layer']} 存在未闭合轮廓。")

    all_points = []
    for feature in features:
        for point in feature["points"]:
            all_points.append((point["x"], point["y"]))

    part_bbox = bbox_from_points(all_points) if all_points else {
        "min_x": 0.0,
        "min_y": 0.0,
        "max_x": 0.0,
        "max_y": 0.0,
        "width": 0.0,
        "height": 0.0,
    }

    feature_summary = {}
    for feature in features:
        feature_summary[feature["type"]] = feature_summary.get(feature["type"], 0) + 1

    return {
        "ok": True,
        "file_name": os.path.basename(dxf_path),
        "feature_count": len(features),
        "feature_summary": feature_summary,
        "entity_summary": dict(sorted(entity_stats.items())),
        "unsupported_entities": dict(sorted(unsupported_stats.items())),
        "chain_debug": chain_debug,
        "part_bbox": part_bbox,
        "features": features,
        "warnings": warnings,
    }


def convert_dxf_to_svg(dxf_path, svg_path):
    doc = ezdxf.readfile(dxf_path)
    msp = doc.modelspace()
    backend = SVGBackend()
    ctx = RenderContext(doc)
    frontend = Frontend(ctx, backend)
    frontend.draw_layout(msp, finalize=True)

    page = Page(0, 0)
    svg_string = backend.get_string(page)
    svg_string = svg_string.replace('stroke="#000000"', 'stroke="#FF5252"')
    svg_string = svg_string.replace('stroke="#ffffff"', 'stroke="#FF5252"')
    svg_string = svg_string.replace('stroke="black"', 'stroke="#FF5252"')
    svg_string = svg_string.replace('stroke="white"', 'stroke="#FF5252"')

    with open(svg_path, "wt", encoding="utf-8") as fp:
        fp.write(svg_string)


def main():
    if len(sys.argv) not in (3, 4):
        print("ERROR: 参数错误，示例: dxf2svg.py input.dxf output.svg [result.json]")
        return

    dxf_path = sys.argv[1]
    svg_path = sys.argv[2]
    json_path = sys.argv[3] if len(sys.argv) == 4 else None

    try:
        result = recognize_features(dxf_path)
        convert_dxf_to_svg(dxf_path, svg_path)

        if json_path:
            with open(json_path, "wt", encoding="utf-8") as fp:
                json.dump(result, fp, ensure_ascii=False, indent=2)

        print("SUCCESS")
    except Exception as exc:
        print(f"ERROR: {str(exc)}")


if __name__ == "__main__":
    main()
