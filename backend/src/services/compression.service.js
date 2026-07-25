import ApiError from "../utils/ApiError"
import { cleanupWorkspace } from '../utils/cleanupWorkspace'

const compress = async (file) => {
  const uuid = Bun.randomUUIDv7()

  try {
    const inputPath = `./tmp/${uuid}/${file.name}`

    await Bun.write(inputPath, file)

    if (!(await Bun.file(inputPath).exists())) {
      throw new ApiError(500, 'file not written to disk')
    }

    const proc = Bun.spawn(['bin/huffman', '-c', inputPath, inputPath], { stderr: 'pipe', stdout: 'pipe' })

    const exitCode = await proc.exited

    if (exitCode !== 0) {
      const err = await proc.stderr.text()
      throw new ApiError(500, 'file was not compressed', [err.trim()])
    }

    const outputPath = inputPath.concat('.huff')

    const compressedFile = Bun.file(outputPath)

    if (!await compressedFile.exists()) {
      throw new ApiError(500, 'file was not compressed')
    }

    return {
      workspaceId: uuid,
      originalName: file.name,
      originalSize: file.size,
      compressedFileName: `${file.name}.huff`,
      compressedSize: compressedFile.size,
      compressionRatio: (1 - (compressedFile.size / file.size)),
      outputPath: outputPath,
      downloadURL: `/api/v1/download/${uuid}/${file.name}.huff`
    }
  } catch (error) {
    cleanupWorkspace(`tmp/${uuid}`)
    throw error
  }
}

export { compress }
