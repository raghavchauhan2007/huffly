import { MAX_FILE_SIZE } from "../constants"
import { decompress } from "../services/decompression.service"
import ApiError from "../utils/ApiError"
import ApiResponse from "../utils/ApiResponse"

const decompressFile = async (req) => {
  const formdata = await req.formData()
  const file = await formdata.get('file')

  if (!(file instanceof File)) {
    throw new ApiError(400, 'Upload a valid file')
  }

  if (file.size > MAX_FILE_SIZE) {
    throw new ApiError(413, `Max Upload Size is ${ MAX_FILE_SIZE / 1024 / 1024 } MiB`)
  }

  const decompressionResponse = await decompress(file)

  return Response.json(
    new ApiResponse(
      200,
      decompressionResponse,
      'Decompressed successfully'
    )
  )
}

export { decompressFile }
