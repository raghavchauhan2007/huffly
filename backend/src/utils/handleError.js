import { CORS_HEADERS } from "../constants";
import ApiError from "./ApiError"

const handleError = (error) => {
  if (error instanceof ApiError) {
    return Response.json(
      {
        statusCode: error.statusCode,
        message: error.message,
        error: error.errors,
        success: error.success
      },
      {
        status: error.statusCode,
        headers: CORS_HEADERS
      }
    )
  }

  console.error(error);

  return Response.json(
    {
      status: 500,
      message: 'Internal Server Error',
      error: [],
      success: false
    },
    {
      status: 500,
      headers: CORS_HEADERS
    }
  )
}

export { handleError }
