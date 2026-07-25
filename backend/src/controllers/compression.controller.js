import { compress } from "../services/compression.service"
import ApiError from "../utils/ApiError"
import ApiResponse from "../utils/ApiResponse"

const compressFile = async (req) => {
  const formdata = await req.formData()
  const file = await formdata.get('file')

  if (!(file instanceof File)) {
    throw new ApiError(400, 'Upload a valid file')
  }

  const compressionResponse = await compress(file)

  return Response.json(
    new ApiResponse(
      200,
      compressionResponse,
      'file successfully compressed'
    )
  )
}

export { compressFile }
