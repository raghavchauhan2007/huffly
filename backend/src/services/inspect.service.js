import { TTL } from "../constants"
import ApiError from "../utils/ApiError"
import { cleanupWorkspace } from "../utils/cleanupWorkspace"

const inspect = async (file) => {
  const workspaceId = Bun.randomUUIDv7()
  const workDir = `./tmp/${workspaceId}`
  const filePath = `${workDir}/${file.name}`

  try {
    await Bun.write(filePath, file)
    if (!(await Bun.file(filePath).exists())) {
      throw new ApiError(500, 'File not written to disk')
    }

    const proc = Bun.spawn(['bin/huffman', '-i', filePath], { stderr: 'pipe', stdout: 'pipe' })

    const exitCode = await proc.exited

    if (exitCode !== 0) {
      const err = await proc.stderr.text()
      throw new ApiError(400, 'inspection failed', [err.trim()])
    }

    const jsonOut = JSON.parse(await proc.stdout.text())

    setTimeout(() => {
      cleanupWorkspace(workDir).catch(console.error)
    }, TTL)

    return {
      workspaceId: workspaceId,
      compressedSize: file.size,
      compressionRatio: (1 - (file.size / jsonOut.originalSize)),
      payloadBytes: file.size - jsonOut.headerSize,
      ...jsonOut
    }
  } catch (error) {
    await cleanupWorkspace(workDir)
    throw error
  }
}

export { inspect }
