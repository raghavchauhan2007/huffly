import ApiError from "../utils/ApiError"

const downloadFile = async (req) => {
  const { id, filename } = req.params
  const filepath = `./tmp/${id}/${filename}`
  const fileResponse = Bun.file(filepath)

  if (!await fileResponse.exists()) {
    throw new ApiError(404, 'file does not exist on server')
  }

  return new Response(fileResponse)
}
export { downloadFile }
