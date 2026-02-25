#include "MeshAsset.h"
#include <iostream>
#include <algorithm>

// ---- MeshData --------------------------------------------------------------

void MeshData::computeAABB()
{
    aabbMin = { 1e9f, 1e9f, 1e9f};
    aabbMax = {-1e9f,-1e9f,-1e9f};
    for (auto& v : vertices) {
        aabbMin = glm::min(aabbMin, v.pos);
        aabbMax = glm::max(aabbMax, v.pos);
    }
}

// ---- GpuMesh ---------------------------------------------------------------

void GpuMesh::destroy()
{
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo);      vbo = 0; }
    if (ebo) { glDeleteBuffers(1, &ebo);      ebo = 0; }
    indexCount = 0;
}

// ---- MeshAsset -------------------------------------------------------------

bool MeshAsset::upload()
{
    if (data.vertices.empty() || data.indices.empty()) {
        std::cerr << "[MeshAsset] '" << name << "' has no data to upload\n";
        return false;
    }

    unload();  // free any existing GPU resources

    glGenVertexArrays(1, &gpu.vao);
    glGenBuffers(1, &gpu.vbo);
    glGenBuffers(1, &gpu.ebo);

    glBindVertexArray(gpu.vao);

    // Interleaved: pos(3) + normal(3) + uv(2) = 8 floats per vertex
    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(data.vertices.size() * sizeof(MeshVertex)),
                 data.vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(data.indices.size() * sizeof(unsigned int)),
                 data.indices.data(), GL_STATIC_DRAW);

    // position  — location 0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          (void*)offsetof(MeshVertex, pos));
    glEnableVertexAttribArray(0);

    // normal    — location 1
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          (void*)offsetof(MeshVertex, normal));
    glEnableVertexAttribArray(1);

    // uv        — location 2
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                          (void*)offsetof(MeshVertex, uv));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    gpu.indexCount = (int)data.indices.size();

    data.computeAABB();

    std::cout << "[MeshAsset] Uploaded '" << name << "': "
              << data.vertices.size() << " verts, "
              << data.indices.size()/3 << " tris\n";
    return true;
}

void MeshAsset::unload()
{
    gpu.destroy();
}
