/* TA-LIB Copyright (c) 1999-2003, Mario Fortier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 * - Neither name of author nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  PK       Pawel Konieczny
 *
 *
 * Change history:
 *
 *  MMDDYY BY   Description
 *  -------------------------------------------------------------------
 *  110903 PK   First version.
 *
 */

/* Description:
 *    This file implements the SQL minidriver using the ODBC library
 *    It is build in the project only when TA_SUPPORT_ODBC is enabled
 */

#ifdef TA_SUPPORT_ODBC

/**** Headers ****/
#if defined(WIN32)
#include "WINDOWS.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sql.h>
#if defined(WIN32)
#include <sqlext.h>
#endif

#include "ta_system.h"
#include "ta_trace.h"
#include "ta_sql_handle.h"
#include "ta_sql_minidriver.h"
#include "ta_sql_odbc.h"


/**** External functions declarations. ****/
/* None */

/**** External variables declarations. ****/
/* None */

/**** Global variables definitions.    ****/
/* None */

/**** Local declarations.              ****/

typedef struct 
{
   SQLCHAR name[TA_SQL_MAX_COLUMN_NAME_SIZE];
   SQLSMALLINT datatype;
   SQLSMALLINT ctype;
   SQLUINTEGER size;

} TA_SQL_ODBC_ColumnDef;



typedef struct
{
   SQLHSTMT hstmt;  /* statement handle */

   SQLINTEGER  numRows;
   SQLSMALLINT numCols;

   TA_SQL_ODBC_ColumnDef *columns;

   int curRow;
   char **rowData;  /* fields to be bound to columns */
   SQLINTEGER *rowIndicator;  /* NULL data indicator array for bound fields */

} TA_SQL_ODBC_Statement;


typedef struct
{
   SQLHENV henv;  /* environment handle */
   SQLHDBC hdbc;  /* connection handle */
   
} TA_SQL_ODBC_Connection;



/**** Local functions declarations.    ****/

/* functions for allocating and freeing rowData field in TA_SQL_ODBC_Statement
 * making use of information in columns array
 */
TA_RetCode allocRowData( TA_SQL_ODBC_Statement *statement );
TA_RetCode freeRowData( TA_SQL_ODBC_Statement *statement );

/**** Local variables definitions.     ****/
TA_FILE_INFO;

/**** Global functions definitions.    ****/



TA_RetCode TA_SQL_ODBC_OpenConnection(const char database[], 
                                      const char host[], 
                                      const char username[], 
                                      const char password[],
                                      unsigned int port,
                                      void **connection)
{
   TA_PROLOG
   TA_SQL_ODBC_Connection *privConnection;
   SQLRETURN sqlRetCode;

   TA_TRACE_BEGIN( TA_SQL_ODBC_OpenConnection );

   TA_ASSERT( connection != NULL );

   privConnection = (TA_SQL_ODBC_Connection*)TA_Malloc( sizeof(TA_SQL_ODBC_Connection) );
   if( privConnection == NULL )
   {
      TA_TRACE_RETURN( TA_ALLOC_ERR );
   }
   memset(privConnection, 0, sizeof(TA_SQL_ODBC_Connection) );


   /* load the Driver Manager and allocate the environment handle */
   sqlRetCode = SQLAllocHandle( SQL_HANDLE_ENV, 
                                SQL_NULL_HANDLE, 
                                &privConnection->henv);
   if( ! SQL_SUCCEEDED(sqlRetCode) )
   {
      TA_Free( privConnection );
      TA_TRACE_RETURN( TA_ALLOC_ERR );
   }

   /* register the version of ODBC to which the driver conforms */
   sqlRetCode = SQLSetEnvAttr( privConnection->henv, 
                               SQL_ATTR_ODBC_VERSION, 
                               (SQLPOINTER)SQL_OV_ODBC3, 0);
   if( ! SQL_SUCCEEDED(sqlRetCode) )
   {
      SQLFreeHandle( SQL_HANDLE_ENV, privConnection->henv );
      TA_Free( privConnection );
      TA_TRACE_RETURN( TA_INTERNAL_ERR );
   }

   /* allocate a connection handle */
   sqlRetCode = SQLAllocHandle( SQL_HANDLE_DBC, 
                                privConnection->henv, 
                                &privConnection->hdbc);
   if( ! SQL_SUCCEEDED(sqlRetCode) )
   {
      SQLFreeHandle( SQL_HANDLE_ENV, privConnection->henv );
      TA_Free( privConnection );
      TA_TRACE_RETURN( TA_ALLOC_ERR );
   }

   /* set any connection attributes */
   sqlRetCode = SQLSetConnectAttr( privConnection->hdbc, 
                                   SQL_ATTR_ACCESS_MODE, 
                                   (SQLPOINTER)SQL_MODE_READ_ONLY, 0);
   if( ! SQL_SUCCEEDED(sqlRetCode) )
   {
      SQLFreeHandle( SQL_HANDLE_DBC, privConnection->hdbc );
      SQLFreeHandle( SQL_HANDLE_ENV, privConnection->henv );
      TA_Free( privConnection );
      TA_TRACE_RETURN( TA_INTERNAL_ERR );
   }
   
   /* connects to the data source */
   sqlRetCode = SQLConnect( privConnection->hdbc, 
                            (SQLCHAR*)database, SQL_NTS,
                            (SQLCHAR*)username, SQL_NTS,
                            (SQLCHAR*)password, SQL_NTS);
   
   if( ! SQL_SUCCEEDED(sqlRetCode) )
   {
      SQLFreeHandle( SQL_HANDLE_DBC, privConnection->hdbc );
      SQLFreeHandle( SQL_HANDLE_ENV, privConnection->henv );
      TA_Free( privConnection );
      TA_TRACE_RETURN( TA_ACCESS_FAILED );
   }

   *connection = privConnection;

   TA_TRACE_RETURN( TA_SUCCESS );
}



TA_RetCode TA_SQL_ODBC_ExecuteQuery(void *connection, 
                                    const char sql_query[], 
                                    void **query_result)
{
   TA_PROLOG
   TA_SQL_ODBC_Connection *privConnection;
   TA_SQL_ODBC_Statement *privStatement;
   int col;
   SQLRETURN sqlRetCode;

   TA_TRACE_BEGIN( TA_SQL_ODBC_ExecuteQuery );

   
   TA_ASSERT( connection != NULL );
   TA_ASSERT( query_result != NULL );

   privConnection = (TA_SQL_ODBC_Connection*)connection;

   privStatement = TA_Malloc( sizeof(TA_SQL_ODBC_Statement) );
   if( privStatement == NULL )
   {
      TA_TRACE_RETURN( TA_ALLOC_ERR );
   }
   memset(privStatement, 0, sizeof(TA_SQL_ODBC_Statement) );

   sqlRetCode = SQLAllocHandle( SQL_HANDLE_STMT, 
                                privConnection->hdbc, 
                                &privStatement->hstmt );
   if( ! SQL_SUCCEEDED(sqlRetCode) )
   {
      TA_Free(privStatement);
      TA_TRACE_RETURN( TA_ALLOC_ERR );
   }

#define RETURN_ON_ERROR( errcode )                              \
   if( ! SQL_SUCCEEDED( sqlRetCode ) )                          \
   {                                                            \
      SQLFreeHandle( SQL_HANDLE_STMT, privStatement->hstmt );   \
      freeRowData( privStatement );                             \
      if( privStatement->columns )                              \
         TA_Free( privStatement->columns );                     \
      TA_Free(privStatement);                                   \
      TA_TRACE_RETURN( errcode );                               \
   }

   sqlRetCode = SQLSetStmtAttr( privStatement->hstmt, 
                                SQL_ATTR_CONCURRENCY, 
                                (SQLPOINTER)SQL_CONCUR_READ_ONLY, 0 );
   RETURN_ON_ERROR( TA_INTERNAL_ERR );
   
   sqlRetCode = SQLExecDirect( privStatement->hstmt, (SQLCHAR*)sql_query, SQL_NTS );
   RETURN_ON_ERROR( TA_BAD_QUERY );

   sqlRetCode = SQLNumResultCols( privStatement->hstmt, &privStatement->numCols );
   RETURN_ON_ERROR( TA_BAD_QUERY );

   /* Officially, SQLRowCount may not be supported for SELECT queries,
    * but many ODBC drivers support it anyway, and it is very handy here to get it
    * to allocate proper size arrays in the upper part of the ta_sql driver 
    */
   sqlRetCode = SQLRowCount( privStatement->hstmt, &privStatement->numRows );
   RETURN_ON_ERROR( TA_NOT_SUPPORTED );

   /* collect information about the columns */
   privStatement->columns = TA_Malloc( privStatement->numCols * sizeof(TA_SQL_ODBC_ColumnDef) );
   if ( privStatement->columns == NULL ) {
      sqlRetCode = SQL_ERROR; /* to force RETURN_ON_ERROR to return */
      RETURN_ON_ERROR( TA_ALLOC_ERR );
   }
   memset( privStatement->columns, 0, privStatement->numCols * sizeof(TA_SQL_ODBC_ColumnDef) );

   for( col = 0;  col < privStatement->numCols;  col++)
   {
      sqlRetCode = SQLDescribeCol( privStatement->hstmt,
                                   (SQLSMALLINT)(col + 1),  /* ODBC counts columns starting at 1 */
                                   privStatement->columns[col].name,
                                   TA_SQL_MAX_COLUMN_NAME_SIZE,
                                   NULL,  /* actual name length is not interesting */
                                   &privStatement->columns[col].datatype,
                                   &privStatement->columns[col].size,
                                   /* other fields are not interesting */
                                   NULL, NULL );
      RETURN_ON_ERROR( TA_INTERNAL_ERR );
   }

   /* prepare buffers for row data */
   allocRowData( privStatement );
   if( privStatement->rowData == NULL || privStatement->rowIndicator == NULL ) 
   {
      sqlRetCode = SQL_ERROR; /* to force RETURN_ON_ERROR to return */
      RETURN_ON_ERROR( TA_ALLOC_ERR );
   }

   for( col = 0;  col < privStatement->numCols;  col++)
   {
      if( privStatement->rowData[col] )
      {
         sqlRetCode = SQLBindCol( privStatement->hstmt,
            (SQLSMALLINT)(col + 1),                /* ODBC counts columns starting at 1 */
            privStatement->columns[col].ctype,
            privStatement->rowData[col],
            privStatement->columns[col].size + 1,  /* relevant (used) only for SQL_C_CHAR columns */
            &privStatement->rowIndicator[col]);    /* to recognize NULL fields */
         RETURN_ON_ERROR( TA_INTERNAL_ERR );
      }
   }
   privStatement->curRow = -1;  /* no rows fetched yet */

#undef RETURN_ON_ERROR

   *query_result = privStatement;
   TA_TRACE_RETURN( TA_SUCCESS );
}



TA_RetCode TA_SQL_ODBC_GetNumColumns(void *query_result, int *num)
{
   TA_PROLOG
   TA_SQL_ODBC_Statement *privStatement;
    
   TA_TRACE_BEGIN( TA_SQL_ODBC_GetNumColumns );

   TA_ASSERT( query_result != NULL );
   TA_ASSERT( num != NULL );

   privStatement = (TA_SQL_ODBC_Statement*)query_result;
   
   *num = privStatement->numCols;
   TA_TRACE_RETURN( TA_SUCCESS );
}



TA_RetCode TA_SQL_ODBC_GetNumRows(void *query_result, int *num)
{
   TA_PROLOG
   TA_SQL_ODBC_Statement *privStatement;

   TA_TRACE_BEGIN( TA_SQL_ODBC_GetNumRows );

   TA_ASSERT( query_result != NULL );
   TA_ASSERT( num != NULL );
   
   privStatement = (TA_SQL_ODBC_Statement*)query_result;
   
   *num = privStatement->numRows;
   TA_TRACE_RETURN( TA_SUCCESS );
}



TA_RetCode TA_SQL_ODBC_GetColumnName(void *query_result, int column, const char **name)
{
   TA_PROLOG
   TA_SQL_ODBC_Statement *privStatement;

   TA_TRACE_BEGIN( TA_SQL_ODBC_GetColumnName );

   TA_ASSERT( query_result != NULL );
   TA_ASSERT( name != NULL );

   privStatement = (TA_SQL_ODBC_Statement*)query_result;
   TA_ASSERT( column < privStatement->numCols );

   *name = privStatement->columns[column].name;
   TA_TRACE_RETURN( TA_SUCCESS );
}



TA_RetCode TA_SQL_ODBC_GetRowString(void *query_result, int row, int column, const char **value)
{
   TA_PROLOG
   TA_SQL_ODBC_Statement *privStatement;
   SQLRETURN sqlRetCode;

   TA_TRACE_BEGIN( TA_SQL_ODBC_GetRowString );

   TA_ASSERT( query_result != NULL );
   TA_ASSERT( value != NULL );

   privStatement = (TA_SQL_ODBC_Statement*)query_result;
   TA_ASSERT( row >= privStatement->curRow );
 
   while( row > privStatement->curRow )
   {
      sqlRetCode = SQLFetch( privStatement->hstmt );
      if( ! SQL_SUCCEEDED(sqlRetCode) )
      {
         TA_TRACE_RETURN( TA_BAD_QUERY );
      }
      privStatement->curRow++;
   }

   if( privStatement->columns[column].ctype != SQL_C_CHAR )
   {
      TA_TRACE_RETURN( TA_NOT_SUPPORTED );
   }

   if( privStatement->rowIndicator[column] == SQL_NULL_DATA )
   {
      *value = "";
   }
   else
   {
      *value = privStatement->rowData[column];
   }
   TA_TRACE_RETURN( TA_SUCCESS );
}



TA_RetCode TA_SQL_ODBC_GetRowReal(void *query_result, int row, int column, TA_Real *value)
{
   TA_PROLOG
   TA_SQL_ODBC_Statement *privStatement;
   SQLRETURN sqlRetCode;

   TA_TRACE_BEGIN( TA_SQL_ODBC_GetRowReal );

   TA_ASSERT( query_result != NULL );
   TA_ASSERT( value != NULL );
   
   privStatement = (TA_SQL_ODBC_Statement*)query_result;
   TA_ASSERT( row >= privStatement->curRow );
   
   while( row > privStatement->curRow )
   {
      sqlRetCode = SQLFetch( privStatement->hstmt );
      if( ! SQL_SUCCEEDED(sqlRetCode) )
      {
         TA_TRACE_RETURN( TA_BAD_QUERY );
      }
      privStatement->curRow++;
   }
   
   if( privStatement->columns[column].ctype != SQL_C_DOUBLE )
   {
      TA_TRACE_RETURN( TA_NOT_SUPPORTED );
   }
   
   if( privStatement->rowIndicator[column] == SQL_NULL_DATA )
   {
      *value = 0.0;
   }
   else
   {
      *value = *(double*)privStatement->rowData[column];
   }
   TA_TRACE_RETURN( TA_SUCCESS );
}



TA_RetCode TA_SQL_ODBC_GetRowInteger(void *query_result, int row, int column, TA_Integer *value)
{
   TA_PROLOG
   TA_SQL_ODBC_Statement *privStatement;
   SQLRETURN sqlRetCode;

   TA_TRACE_BEGIN( TA_SQL_ODBC_GetRowInteger );

   TA_ASSERT( query_result != NULL );
   TA_ASSERT( value != NULL );
   
   privStatement = (TA_SQL_ODBC_Statement*)query_result;
   TA_ASSERT( row >= privStatement->curRow );
   
   while( row > privStatement->curRow )
   {
      sqlRetCode = SQLFetch( privStatement->hstmt );
      if( ! SQL_SUCCEEDED(sqlRetCode) )
      {
         TA_TRACE_RETURN( TA_BAD_QUERY );
      }
      privStatement->curRow++;
   }
   
   if( privStatement->columns[column].ctype != SQL_C_SLONG )
   {
      TA_TRACE_RETURN( TA_NOT_SUPPORTED );
   }
   
   if( privStatement->rowIndicator[column] == SQL_NULL_DATA )
   {
      *value = 0;
   }
   else
   {
      *value = *(long*)privStatement->rowData[column];
   }
   TA_TRACE_RETURN( TA_SUCCESS );
}



TA_RetCode TA_SQL_ODBC_ReleaseQuery(void *query_result)
{
   TA_PROLOG
   TA_SQL_ODBC_Statement *privStatement;
   SQLRETURN sqlRetCode;

   TA_TRACE_BEGIN( TA_SQL_ODBC_ReleaseQuery );

   TA_ASSERT( query_result != NULL );

   privStatement = (TA_SQL_ODBC_Statement*)query_result;

   sqlRetCode = SQLFreeHandle( SQL_HANDLE_STMT, privStatement->hstmt );
   freeRowData( privStatement );
   if( privStatement->columns )
   {
      TA_Free( privStatement->columns );
   }
   TA_Free(privStatement);

   if( ! SQL_SUCCEEDED(sqlRetCode) )
   {
      TA_TRACE_RETURN( TA_INVALID_HANDLE );
   }

   TA_TRACE_RETURN( TA_SUCCESS );
}



TA_RetCode TA_SQL_ODBC_CloseConnection(void *connection)
{
   TA_PROLOG
   TA_SQL_ODBC_Connection *privConnection;
   SQLRETURN sqlRetCode;

   TA_TRACE_BEGIN( TA_SQL_ODBC_CloseConnection );

   privConnection = (TA_SQL_ODBC_Connection*)connection;
   if ( privConnection )
   {
      sqlRetCode = SQLDisconnect( privConnection->hdbc );
      if( ! SQL_SUCCEEDED(sqlRetCode) )
      {
         TA_TRACE_RETURN( TA_INTERNAL_ERR );
      }

      sqlRetCode = SQLFreeHandle( SQL_HANDLE_DBC, privConnection->hdbc );
      if( ! SQL_SUCCEEDED(sqlRetCode) )
      {
         TA_TRACE_RETURN( TA_INVALID_HANDLE );
      }
      
      sqlRetCode = SQLFreeHandle( SQL_HANDLE_ENV, privConnection->henv );
      if( ! SQL_SUCCEEDED(sqlRetCode) )
      {
         TA_TRACE_RETURN( TA_INVALID_HANDLE );
      }
      
      TA_Free( privConnection );
   }

   TA_TRACE_RETURN( TA_SUCCESS );
}



/**** Local functions definitions.     ****/


TA_RetCode allocRowData( TA_SQL_ODBC_Statement *statement )
{
   int col;

   statement->rowData = TA_Malloc( statement->numCols * sizeof(char*) );
   if( statement->rowData == NULL )
   {
      return TA_ALLOC_ERR;
   }
   memset( statement->rowData, 0, statement->numCols * sizeof(char*) );

   statement->rowIndicator = TA_Malloc( statement->numCols * sizeof(SQLINTEGER) );
   if( statement->rowIndicator == NULL )
   {
      return TA_ALLOC_ERR;
   }
   memset( statement->rowIndicator, 0, statement->numCols * sizeof(SQLINTEGER) );
   
   for( col = 0;  col < statement->numCols;  col++)
   {
      int size = 0;
      SQLSMALLINT ctype;

      switch( statement->columns[col].datatype ) 
      {
         case SQL_CHAR:
         case SQL_VARCHAR:
         case SQL_TYPE_TIME:
         case SQL_TYPE_DATE:
            size = statement->columns[col].size + 1;  /* add 1 for terminating '\0' */
            ctype = SQL_C_CHAR;
         	break;

         case SQL_DECIMAL:
         case SQL_NUMERIC:
            size = sizeof( double );
            ctype = SQL_C_DOUBLE;
            break;

         case SQL_SMALLINT:
         case SQL_INTEGER:
            size = sizeof( long );
            ctype = SQL_C_SLONG;
            break;

         default:
            ; /* ignore other column types */
      }
      if( size > 0 )
      {
         statement->rowData[col] = TA_Malloc(size);
         if( statement->rowData[col] == NULL )
         {
            freeRowData( statement );
            return TA_ALLOC_ERR;
         }
         statement->columns[col].ctype = ctype;
      }
   }
   
   return TA_SUCCESS;
}



TA_RetCode freeRowData( TA_SQL_ODBC_Statement *statement )
{
   int col;

   if( statement->rowData != NULL )
   {
      for( col = 0;  col < statement->numCols;  col++)
      {
         if( statement->rowData[col] != NULL )
         {
            TA_Free( statement->rowData[col] );
         }
      }
      TA_Free( statement->rowData );
      statement->rowData = NULL;
   }

   if( statement->rowIndicator != NULL)
   {
      TA_Free( statement->rowIndicator );
      statement->rowIndicator = NULL;
   }

   return TA_SUCCESS;
}


#endif