import ApiError from "../utils/ApiError"

const decompress = async (file) => {
  const workspaceId = Bun.randomUUIDv7()
  const workDir = `./tmp/${workspaceId}`
  const filePath = `${workDir}/${file.name}`

  await Bun.write(filePath, file)
  if (!(await Bun.file(filePath).exists())) {
    throw new ApiError(500, 'File not written to disk')
  }

  const proc = Bun.spawn(['bin/huffman', '-x', 'filePath', 'workDir'], { stderr: 'pipe', stdout: 'pipe' })

  const exitCode = await proc.exited

  if (exitCode !== 0) {
    const err = await proc.stderr.text()
    throw new ApiError(500, 'file not decompressed', [err.trim()])
  }
}

export { decompress }
