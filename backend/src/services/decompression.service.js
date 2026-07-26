import ApiError from "../utils/ApiError"
import { cleanupWorkspace } from "../utils/cleanupWorkspace"

const decompress = async (file) => {
  const workspaceId = Bun.randomUUIDv7()
  const workDir = `./tmp/${workspaceId}`
  const filePath = `${workDir}/${file.name}`

  try {
    await Bun.write(filePath, file)
    if (!(await Bun.file(filePath).exists())) {
      throw new ApiError(500, 'File not written to disk')
    }

    const proc = Bun.spawn(['bin/huffman', '-x', filePath, workDir], { stderr: 'pipe', stdout: 'pipe' })

    const exitCode = await proc.exited

    if (exitCode !== 0) {
      const err = await proc.stderr.text()
      throw new ApiError(400, 'decompression failed', [err.trim()])
    }

    const { outputPath, originalFilename, originalSize } = JSON.parse(await proc.stdout.text())

    const decompressedFile = Bun.file(outputPath)

    if (!(await decompressedFile.exists())) {
      throw new ApiError(500, 'decompression failed')
    }

    setTimeout(() => {
      cleanupWorkspace(`./tmp/${workspaceId}`).catch(console.error)
    }, 30 * 60 * 1000)

    return {
      workspaceId: workspaceId,
      originalName: originalFilename,
      originalSize: originalSize,
      compressedFileName: file.name,
      compressedSize: file.size,
      compressionRatio: (1 - (file.size / originalSize)),
      outputPath: outputPath,
      downloadURL: `/api/v1/download/${workspaceId}/${originalFilename}`
    }
  } catch (error) {
    await cleanupWorkspace(workDir)
    throw error
  }
}

export { decompress }
