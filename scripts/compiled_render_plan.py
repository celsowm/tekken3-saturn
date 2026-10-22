"""Compile private SKA topology without duplicating its vertex/face records."""


def compile_plan(skin, indices, double_sided):
    count = len(skin.rigid) + len(skin.blend)
    if not count or count > 65535 or len(indices) != len(double_sided):
        raise ValueError('invalid compiled render shape')
    if len(indices) > 65535 or not 0 < skin.joint_count <= 255:
        raise ValueError('compiled render indices exceed uint16 storage')
    groups = [[] for _ in range(skin.joint_count)]
    dependencies = [[] for _ in range(count)]
    joint_mask = 0
    maximum_l1 = 0
    exact = skin.pos_shift == 15 and skin.max_joint < 18
    for vertex, (position, joint) in enumerate(skin.rigid):
        groups[joint].append(vertex)
        joint_mask |= 1 << joint
        maximum_l1 = max(maximum_l1, sum(abs(v) for v in position))
    for joints, influences, weights, positions in skin.blend:
        exact &= influences == 2 and weights[0] == 16384
        for joint, position in zip(joints[:influences], positions[:influences]):
            joint_mask |= 1 << joint
            maximum_l1 = max(maximum_l1, sum(abs(v) for v in position))
    face_joints = []
    for face, corners in enumerate(indices):
        if len(corners) != 4 or any(v < 0 or v >= count for v in corners):
            raise ValueError('invalid compiled face indices')
        for vertex in sorted(set(corners)):
            dependencies[vertex].append(face)
        joint = 255
        if not double_sided[face] and all(v < len(skin.rigid) for v in corners):
            joints = {skin.rigid[v][1] for v in corners}
            if len(joints) == 1:
                joint = joints.pop()
        face_joints.append(joint)

    def flatten(rows):
        offsets, values = [0], []
        for row in rows:
            values.extend(row)
            offsets.append(len(values))
        if len(values) > 65535:
            raise ValueError('compiled dependencies exceed uint16 storage')
        return offsets, values

    joint_offsets, rigid_vertices = flatten(groups)
    vertex_offsets, vertex_faces = flatten(dependencies)
    # A full signed-16 rotation, including +32768 after basis conversion.
    exact &= maximum_l1 <= (2147483647 - 1024) // 32768
    return dict(schema=1, vertex_count=count, face_count=len(indices),
                joint_count=skin.joint_count, joint_mask=joint_mask,
                maximum_l1=maximum_l1, exact_skin_supported=bool(exact),
                joint_offsets=joint_offsets, rigid_vertices=rigid_vertices,
                vertex_offsets=vertex_offsets, vertex_faces=vertex_faces,
                face_joints=face_joints)


def emit_c(plan):
    """Only the opt-in build pays for the additional immutable data.

    .wram_l is NOLOAD in the installed SDK: initialized constants must stay in
    the loaded image, rather than silently reading uninitialized Low WRAM.
    """
    lines = ['#if defined(T3_COMPILED_RENDERER)', '#include "compiled_renderer.h"']
    for name in ('joint_offsets', 'rigid_vertices', 'vertex_offsets', 'vertex_faces', 'face_joints'):
        values = plan[name]
        ctype = 'uint8_t' if name == 'face_joints' else 'uint16_t'
        lines.append(f'static const {ctype} render_{name}[{max(1, len(values))}] = {{')
        for start in range(0, len(values), 24):
            lines.append('    ' + ','.join(str(v) for v in values[start:start+24]) + ',')
        if not values:
            lines.append('    0,')
        lines.append('};')
    lines.extend(['const t3_compiled_plan_t t3_compiled_plan = {',
                  f'    {plan["vertex_count"]}u,{plan["face_count"]}u,{plan["joint_count"]}u,',
                  f'    {len(plan["vertex_faces"])}u,{plan["maximum_l1"]}u,',
                  f'    {plan["joint_mask"]}u,{int(plan["exact_skin_supported"])}u,',
                  '    render_joint_offsets,render_rigid_vertices,',
                  '    render_vertex_offsets,render_vertex_faces,render_face_joints',
                  '};', '#endif'])
    return lines
