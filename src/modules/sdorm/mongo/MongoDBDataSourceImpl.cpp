/*
 * MongoDBDataSourceImpl.cpp
 *
 *  Created on: 02-Jun-2014
 *      Author: sumeetc
 */

#include "MongoDBDataSourceImpl.h"

string MongoDBDataSourceImpl::initializeDMLQueryParts(Query& cquery, bson_t** data, bson_t** query, string& operationName) {
	string qs = cquery.getQuery();
	StringUtil::trim(qs);
	string collectionName;
	if(qs.find("db.")!=string::npos && qs.find("(")!=string::npos && qs.at(qs.length()-1)==')') {
		string querySchemaPart = qs.substr(3);
		querySchemaPart = querySchemaPart.substr(0, querySchemaPart.find("("));
		StringUtil::trim(querySchemaPart);

		if(querySchemaPart.find(".")==string::npos)
			throw "Invalid querySchemaPart specified";

		collectionName = querySchemaPart.substr(0, querySchemaPart.find("."));
		if(collectionName=="")
			throw "No collection name specified";

		operationName = querySchemaPart.substr(querySchemaPart.find(".")+1);
		if(operationName=="")
			throw "No operation name specified";

		string queryStrPart = qs.substr(qs.find("(")+1);
		queryStrPart = queryStrPart.substr(0, queryStrPart.length()-1);
		StringUtil::trim(queryStrPart);

		if(queryStrPart.at(0)!='{' || queryStrPart.at(queryStrPart.length()-1)!='}')
			throw "Invalid JSON query parts specified";

		if(operationName=="insert") {
			bson_error_t err;
			*data = bson_new_from_json((const uint8_t*)queryStrPart.c_str(), queryStrPart.length(), &err);
		} else {
			bson_error_t err;
			bson_t* queryParts = bson_new_from_json((const uint8_t*)queryStrPart.c_str(), queryStrPart.length(), &err);
			bson_iter_t i;

			bson_iter_init(&i, queryParts);
			bool found = bson_iter_find(&i, "$data");
			if(found)
			{
				*data = bson_new_from_data(bson_iter_value(&i)->value.v_doc.data, bson_iter_value(&i)->value.v_doc.data_len);
			}

			bson_iter_init(&i, queryParts);
			found = bson_iter_find(&i, "$query");
			if(found)
			{
				*query = bson_new_from_data(bson_iter_value(&i)->value.v_doc.data, bson_iter_value(&i)->value.v_doc.data_len);
			}
		}
	}
	return collectionName;
}

string MongoDBDataSourceImpl::initializeQueryParts(Query& cquery, bson_t** querySpec, bson_t** fields, string& operationName) {
	*fields = NULL;
	string qs = cquery.getQuery();
	StringUtil::trim(qs);
	string collectionName;
	if(qs.find("db.")!=string::npos && qs.find("(")!=string::npos && qs.at(qs.length()-1)==')') {
		string querySchemaPart = qs.substr(3);
		querySchemaPart = querySchemaPart.substr(0, querySchemaPart.find("("));
		StringUtil::trim(querySchemaPart);

		if(querySchemaPart.find(".")==string::npos)
			throw "Invalid querySchemaPart specified";

		collectionName = querySchemaPart.substr(0, querySchemaPart.find("."));
		if(collectionName=="")
			throw "No collection name specified";

		operationName = querySchemaPart.substr(querySchemaPart.find(".")+1);
		if(operationName=="")
			throw "No operation name specified";

		string queryStrPart = qs.substr(qs.find("(")+1);
		queryStrPart = queryStrPart.substr(0, queryStrPart.length()-1);
		StringUtil::trim(queryStrPart);

		if(queryStrPart.at(0)!='{' || queryStrPart.at(queryStrPart.length()-1)!='}')
			throw "Invalid JSON query parts specified";

		bson_error_t err;
		bson_t* queryParts = bson_new_from_json((const uint8_t*)queryStrPart.c_str(), queryStrPart.length(), &err);
		bson_iter_t i;
		cout << bson_as_json(queryParts, NULL) << endl;

		bson_iter_init(&i, queryParts);
		bool t = bson_iter_find(&i, "$query");
		if(t)
		{
			bson_append_value(*querySpec, "$query", 6, bson_iter_value(&i));
		}

		bson_iter_init(&i, queryParts);
		t = bson_iter_find(&i, "$orderby");
		if(t)
		{
			bson_append_value(*querySpec, "$orderby", 8, bson_iter_value(&i));
		}

		bson_iter_init(&i, queryParts);
		t = bson_iter_find(&i, "$fields");
		if(t)
		{
			*fields = bson_new_from_data(bson_iter_value(&i)->value.v_doc.data, bson_iter_value(&i)->value.v_doc.data_len);
		}
	}
	return collectionName;
}

QueryComponent* MongoDBDataSourceImpl::getQueryComponent(const vector<Condition>& conds)
{
	QueryComponent* subQuery = new QueryComponent();
	QueryComponent* currentSubQuery = subQuery;

	currentSubQuery->undecided = true;
	currentSubQuery->actualQuery = NULL;
	currentSubQuery->parent = NULL;

	int brackets = 0;
	bool prevCloseFlag = false;

	for (int i=0;i<(int)conds.size();i++)
	{
		if (conds.at(i).getClause()==QueryClause::NONE)
		{
			if(currentSubQuery->undecided)
				currentSubQuery->tempClauses.push_back(conds.at(i));
			else if(currentSubQuery->isAnd)
			{
				currentSubQuery->andClauses.push_back(conds.at(i));
				if(currentSubQuery->tempClauses.size()>0) {
					for (int var = 0; var < (int)currentSubQuery->tempClauses.size(); ++var) {
						currentSubQuery->andClauses.push_back(currentSubQuery->tempClauses.at(var));
					}
					currentSubQuery->tempClauses.clear();
				}
			}
			else
			{
				currentSubQuery->orClauses.push_back(conds.at(i));
				if(currentSubQuery->tempClauses.size()>0) {
					for (int var = 0; var < (int)currentSubQuery->tempClauses.size(); ++var) {
						currentSubQuery->orClauses.push_back(currentSubQuery->tempClauses.at(var));
					}
					currentSubQuery->tempClauses.clear();
				}
			}
		}
		else
		{
			if(conds.at(i).getClause()!=QueryClause::LOGICAL_GRP_CLOSE) {
				prevCloseFlag = false;
			}

			if(conds.at(i).getClause()==QueryClause::LOGICAL_GRP_OPEN) {
				brackets++;
				if(!currentSubQuery->undecided)
				{
					QueryComponent* temp = new QueryComponent;
					temp->undecided = true;
					temp->isAnd = false;
					temp->actualQuery = NULL;
					if(currentSubQuery->isAnd)
						currentSubQuery->andChildren.push_back(temp);
					else
						currentSubQuery->orChildren.push_back(temp);
					temp->parent = currentSubQuery;
					currentSubQuery = temp;
				}
			}
			else if(conds.at(i).getClause()==QueryClause::LOGICAL_GRP_CLOSE) {
				brackets--;
				if(currentSubQuery->parent!=NULL)
				{
					currentSubQuery = currentSubQuery->parent;
				}
				else if(!prevCloseFlag)
				{
					QueryComponent* temp = new QueryComponent;
					temp->tempChildren.push_back(currentSubQuery);
					temp->parent = NULL;
					temp->undecided = true;
					temp->isAnd = false;
					temp->actualQuery = NULL;
					currentSubQuery = temp;
				}
				prevCloseFlag = true;
			}
			else if(conds.at(i).getClause()==QueryClause::AND) {
				currentSubQuery->isAnd = true;
				currentSubQuery->undecided = false;
			}
			else if(conds.at(i).getClause()==QueryClause::OR) {
				currentSubQuery->isAnd = false;
				currentSubQuery->undecided = false;
			}
			if(!currentSubQuery->undecided && currentSubQuery->tempChildren.size()>0) {
				for (int var = 0; var < (int)currentSubQuery->tempChildren.size(); ++var) {
					if(currentSubQuery->isAnd)
					{
						currentSubQuery->andChildren.push_back(currentSubQuery->tempChildren.at(var));
					}
					else
					{
						currentSubQuery->orChildren.push_back(currentSubQuery->tempChildren.at(var));
					}
				}
				currentSubQuery->tempChildren.clear();
			}
		}
	}

	if(currentSubQuery->tempClauses.size()>0) {
		for (int var = 0; var < (int)currentSubQuery->tempClauses.size(); ++var) {
			currentSubQuery->andClauses.push_back(currentSubQuery->tempClauses.at(var));
		}
		currentSubQuery->tempClauses.clear();
	}

	if(brackets!=0) {
		delete currentSubQuery;
		currentSubQuery = NULL;
		throw "Unbalanced paranthesis used in query";
	}

	return currentSubQuery;
}

void MongoDBDataSourceImpl::populateQueryComponents(QueryComponent* sq)
{
	bool hasChildren = false;
	if(sq->andChildren.size()>0)
	{
		hasChildren = true;
		for (QueryComponent* subQ : sq->andChildren) {
			populateQueryComponents(subQ);
		}
	}
	if(sq->orChildren.size()>0)
	{
		hasChildren = true;
		for (QueryComponent* subQ : sq->orChildren) {
			populateQueryComponents(subQ);
		}
	}
	if(sq->andClauses.size()>0 || sq->orClauses.size()>0 || hasChildren)
	{
		vector<bson_t*> andChildQs;
		vector<bson_t*> orChildQs;
		if(sq->andClauses.size()>0)
			andChildQs.push_back(createSubMongoQuery(sq->andClauses));
		if(sq->orClauses.size()>0)
			orChildQs.push_back(createSubMongoQuery(sq->orClauses));
		if(hasChildren)
		{
			for (QueryComponent* subQ : sq->andChildren) {
				if(subQ->actualQuery!=NULL)
				{
					andChildQs.push_back(subQ->actualQuery);
				}
			}
			for (QueryComponent* subQ : sq->orChildren) {
				if(subQ->actualQuery!=NULL)
				{
					orChildQs.push_back(subQ->actualQuery);
				}
			}
		}
		if(andChildQs.size()>0 || orChildQs.size()>0)
		{
			bson_t* dbo = bson_new();
			if(andChildQs.size()>0)
			{
				bson_t *child = bson_new();
				bson_append_array_begin(dbo, "$and", 4, child);
				for (int var = 0; var < (int)andChildQs.size(); ++var) {
					bson_append_document(child, "", 0, andChildQs.at(var));
				}
				bson_append_array_end(dbo, child);
				bson_destroy(child);
			}
			if(orChildQs.size()>0)
			{
				bson_t *child = bson_new();
				bson_append_array_begin(dbo, "$or", 3, child);
				for (int var = 0; var < (int)orChildQs.size(); ++var) {
					bson_append_document(child, "", 0, orChildQs.at(var));
				}
				bson_append_array_end(dbo, child);
				bson_destroy(child);
			}
			sq->actualQuery = dbo;
		}
	}
}

map<string, map<string, Condition> > MongoDBDataSourceImpl::toMap(vector<Condition>& conds) {
	map<string, map<string, Condition> > cols;
	for (int i=0;i<(int)conds.size();i++)
	{
		Condition cond = conds.at(i);
		if(cond.getOper()==QueryOperator::EQUALS) {
			cols[cond.getLhs()]["$eq"] = cond;
		} else if(cond.getOper()==QueryOperator::NOT_EQUALS) {
			cols[cond.getLhs()]["$ne"] = cond;
		} else if(cond.getOper()==QueryOperator::GREATER_THAN) {
			cols[cond.getLhs()]["$gt"] = cond;
		} else if(cond.getOper()==QueryOperator::GREATER_THAN_EQUALS) {
			cols[cond.getLhs()]["$gte"] = cond;
		} else if(cond.getOper()==QueryOperator::LESS_THAN) {
			cols[cond.getLhs()]["$lt"] = cond;
		} else if(cond.getOper()==QueryOperator::LESS_THAN_EQUALS) {
			cols[cond.getLhs()]["$lte"] = cond;
		} else if(cond.getOper()==QueryOperator::IN) {
			cols[cond.getLhs()]["$in"] = cond;
		} else if(cond.getOper()==QueryOperator::NOT_IN) {
			cols[cond.getLhs()]["$nin"] = cond;
		}
	}
	return cols;
}

bson_t* MongoDBDataSourceImpl::createSubMongoQuery(vector<Condition>& conds) {
	bson_t* subQ = bson_new();
	map<string, map<string, Condition> > cols = toMap(conds);
	map<string, map<string, Condition> >::iterator mit;
	for (mit=cols.begin();mit!=cols.end();++mit)
	{
		string propName = mit->first;
		map<string, Condition> condMap = mit->second;
		map<string, Condition>::iterator cit;
		for (cit=condMap.begin();cit!=condMap.end();++cit)
		{
			if(cit->first=="$in" || cit->first=="$nin") {
				bson_t* subNe = bson_new();
				bson_t* child = bson_new();
				bson_append_document_begin(subNe, propName.c_str(), propName.length(), child);
				bson_t* childa = bson_new();
				bson_append_array_begin(child, cit->first.c_str(), cit->first.length(), childa);
				for (int var = 0; var < cit->second.getRhsSize(); ++var) {
					appendGenericObject(childa, "", cit->second.getRhs(var));
				}
				bson_append_array_end(child, childa);
				bson_append_document_end(subNe, child);
				bson_destroy(subNe);
				bson_destroy(child);
				bson_destroy(childa);
			} else if(cit->first!="$eq") {
				bson_t* subNe = bson_new();
				appendGenericObject(subNe, cit->first, cit->second.getRhs());
				bson_append_document(subQ, propName.c_str(), propName.length(), subNe);
				bson_destroy(subNe);
			} else {
				appendGenericObject(subQ, propName, cit->second.getRhs());
			}
		}
	}
	return subQ;
}

void MongoDBDataSourceImpl::appendGenericObject(bson_t* b, const string& name, GenericObject& o) {
	if(o.isInstanceOf("int") || o.isInstanceOf("short") || o.isInstanceOf("long")
			|| o.isInstanceOf("unsigned int") || o.isInstanceOf("unsigned short")) {
		int sv;
		o.get(sv);
		bson_append_int32(b, name.c_str(), name.length(), sv);
	} else if(o.isInstanceOf("unsigned long") || o.isInstanceOf("long long")) {
		long long sv;
		o.get(sv);
		bson_append_int64(b, name.c_str(), name.length(), sv);
	} else if(o.isInstanceOf("std::string")) {
		string sv;
		o.get(sv);
		bson_append_utf8(b, name.c_str(), name.length(), sv.c_str(), sv.length());
	} else if(o.isInstanceOf("bool")) {
		bool sv;
		o.get(sv);
		bson_append_bool(b, name.c_str(), name.length(), sv);
	} else if(o.isInstanceOf("double")) {
		double sv;
		o.get(sv);
		bson_append_double(b, name.c_str(), name.length(), sv);
	} else if(o.isInstanceOf("float")) {
		float sv;
		o.get(sv);
		bson_append_double(b, name.c_str(), name.length(), sv);
	}
}

void* MongoDBDataSourceImpl::getResults(const string& collectionName, Query& query, bson_t* querySpec, bson_t* fields, const bool& isObj, const bool& isCountQuery) {
	Connection* conn = _conn();

	mongoc_collection_t *collection = _collection (conn, collectionName.c_str());

	mongoc_cursor_t* cursor = NULL;

	void* result = NULL;
	string clasName = mapping->getClassForTable(collectionName);
	if(isObj)
	{
		result = reflector->getNewContainer(clasName, "std::vector", appName);
	}
	else if(!isCountQuery)
	{
		result = new vector<map<string, GenericObject> >;
	}

	if(!isCountQuery)
	{
		cursor = mongoc_collection_find(collection, MONGOC_QUERY_NONE, query.getStart(), query.getCount(), 0,
				querySpec, fields, NULL);
		const bson_t *doc;
		while (mongoc_cursor_next(cursor, &doc))
		{
			if(isObj)
			{
				void* ob = getObject((bson_t*)doc, NULL, 0, clasName);
				reflector->addToContainer(result, ob, clasName, "std::vector", appName);
			}
			else
			{
				map<string, GenericObject> row;
				getMapOfProperties((bson_t*)doc, &row);
				((vector<map<string, GenericObject> >*)result)->push_back(row);
			}
		}
		mongoc_cursor_destroy (cursor);
	}
	else
	{
		bson_error_t er;
		result = new long(mongoc_collection_count(collection, MONGOC_QUERY_NONE, querySpec, query.getStart(),
				query.getCount(), NULL, &er));
	}

	bson_destroy(querySpec);
	if(fields!=NULL)bson_destroy(fields);

	_release(conn, collection);
	return result;
}



void* MongoDBDataSourceImpl::getResults(const string& collectionName, QueryBuilder& qb, bson_t* query, bson_t* fields, const bool& isObj) {
	Connection* conn = _conn();

	mongoc_collection_t *collection = _collection (conn, collectionName.c_str());

	mongoc_cursor_t* cursor = NULL;

	bson_t* querySpec = bson_new();

	if(query!=NULL)
	{
		bson_append_document(querySpec, "$query", 6, query);
	}

	if(qb.getColumnsAsc().size()>0 || qb.getColumnsDesc().size()>0)
	{
		bson_t* child = bson_new();
		bson_append_document_begin(querySpec, "$orderby", 8, child);
		for(int i=0;i<(int)qb.getColumnsAsc().size();i++) {
			string colnm = qb.getColumnsAsc().at(i);
			bson_append_int32(child, colnm.c_str(), colnm.length(), 1);
		}
		for(int i=0;i<(int)qb.getColumnsDesc().size();i++) {
			string colnm = qb.getColumnsDesc().at(i);
			bson_append_int32(child, colnm.c_str(), colnm.length(), 1);
		}
		bson_append_document_end(querySpec, child);
		bson_destroy(child);
	}

	void* result = NULL;
	string clasName = mapping->getClassForTable(collectionName);
	if(isObj)
	{
		result = reflector->getNewContainer(clasName, "std::vector", appName);
	}
	else
	{
		result = new vector<map<string, GenericObject> >;
	}

	cursor = mongoc_collection_find(collection, MONGOC_QUERY_NONE, qb.getStart(), qb.getCount(), 0,
			querySpec, fields, NULL);
	const bson_t *doc;
	while (mongoc_cursor_next(cursor, &doc))
	{
		if(isObj)
		{
			void* ob = getObject((bson_t*)doc, NULL, 0, clasName);
			reflector->addToContainer(result, ob, clasName, "std::vector", appName);
		}
		else
		{
			map<string, GenericObject> row;
			getMapOfProperties((bson_t*)doc, &row);
			((vector<map<string, GenericObject> >*)result)->push_back(row);
		}
	}
	mongoc_cursor_destroy (cursor);

	bson_destroy(query);
	bson_destroy(querySpec);
	if(fields!=NULL)bson_destroy(fields);

	_release(conn, collection);
	return result;
}

string MongoDBDataSourceImpl::getQueryForRelationship(const string& column, const string& type, void* val)
{
	string qstr = "{\"" + column + "\":";
	if(type=="string") {
		qstr += "\"" + (*(string*)val) + "\"}";
	} else if(type=="short" || type=="int" || type=="unsigned short" || type=="unsigned int" || type=="long") {
		qstr += CastUtil::lexical_cast<string>(*(long*)val) + "}";
	} else if(type=="bool") {
		qstr += CastUtil::lexical_cast<string>(*(bool*)val) + "}";
	} else if(type=="float" || type=="double") {
		qstr += CastUtil::lexical_cast<string>(*(double*)val) + "}";
	}
	delete val;
	return qstr;
}

void MongoDBDataSourceImpl::storeProperty(const ClassInfo& clas, void* t, void* colV, const Field& fe)
{
	if(colV!=NULL)
	{
		string te = fe.getType();
		args argus;
		argus.push_back(te);
		vals valus;
		valus.push_back(colV);
		string methname = "set"+StringUtil::capitalizedCopy(fe.getFieldName());
		Method meth = clas.getMethod(methname, argus);
		reflector->invokeMethod<void*>(t,meth,valus);
	}
}

void MongoDBDataSourceImpl::executePreTable(DataSourceEntityMapping& dsemp, GenericObject& idv) {
	if(dsemp.getIdgendbEntityType()=="table")
	{
		string query;
		StringContext params;
		params["idgen_tabname"] = dsemp.getIdgendbEntityName();
		params["idgen_colname"] = dsemp.getIdgencolumnName();
		if(dsemp.getIdgentype()=="multihilo" || dsemp.getIdgentype()=="multi")
		{
			params["entity_column"] = dsemp.getIdgenentityColumn();
			params["entity_name"] = dsemp.getTableName();
			query = string("db.${idgen_tabname}.find({\"$query\": {\"${entity_column}\": \"${entity_name}\"}, ") +
						string("\"$fields\": {\"${idgen_colname}\": 1}})");
		}
		else
		{
			query = "db.${idgen_tabname}.find({\"$fields\": {\"${idgen_colname}\": 1}})";
		}
		query = TemplateEngine::evaluate(query, params);

		Query q(query);
		vector<map<string, GenericObject> > vecmp = execute(q);
		if(vecmp.size()>0 && vecmp.at(0).size()>0)
		{
			if(vecmp.at(0).find(dsemp.getIdgencolumnName())!=vecmp.at(0).end())
			{
				idv = vecmp.at(0)[dsemp.getIdgencolumnName()];
			}
		}
	}
}

void MongoDBDataSourceImpl::executePostTable(DataSourceEntityMapping& dsemp, GenericObject& idv) {
	if(dsemp.getIdgendbEntityType()=="table")
	{
		string query;
		StringContext params;
		params["idgen_tabname"] = dsemp.getIdgendbEntityName();
		params["idgen_colname"] = dsemp.getIdgencolumnName();
		if(dsemp.getIdgentype()=="multihilo")
		{
			params["entity_column"] = dsemp.getIdgenentityColumn();
			params["entity_name"] = dsemp.getTableName();
			query = string("db.${idgen_tabname}.update({\"$query\": {\"${entity_column}\": \"${entity_name}\"}, ") +
						string("\"$data\": {\"$inc\": {\"${idgen_colname}\": 1}}})");
		}
		else
		{
			query = "db.${idgen_tabname}.update({\"$data\": {\"$inc\": {\"${idgen_colname}\": 1}}})";
		}
		query = TemplateEngine::evaluate(query, params);

		Query q(query);
		executeUpdate(q);
	}
}

void MongoDBDataSourceImpl::executeSequence(DataSourceEntityMapping& dsemp, GenericObject& idv) {
}

void MongoDBDataSourceImpl::executeIdentity(DataSourceEntityMapping& dsemp, GenericObject& idv) {
}

vector<map<string, GenericObject> > MongoDBDataSourceImpl::execute(Query& query) {
	void* res = executeQuery(query, false);
	vector<map<string, GenericObject> > vec;
	if(res!=NULL) {
		vec = *(vector<map<string, GenericObject> >*)res;
		delete res;
	}
	return vec;

}

MongoDBDataSourceImpl::MongoDBDataSourceImpl(ConnectionPooler* pool, Mapping* mapping) {
	this->pool = pool;
	this->mapping = mapping;
	logger = LoggerFactory::getLogger("MongoDBDataSourceImpl");
}

MongoDBDataSourceImpl::~MongoDBDataSourceImpl() {
}

void MongoDBDataSourceImpl::executeCustom(DataSourceEntityMapping& dsemp, const string& customMethod, GenericObject& idv) {
	if(dsemp.getIdgendbEntityType().find("custom:")==0)
	{
		if(customMethod=="oid") {
			bson_oid_t oid;
			bson_oid_init (&oid, NULL);
			char buffer[100];
			memset(buffer, 0, sizeof(buffer));
			bson_oid_to_string(&oid, buffer);
			string oidv = string(buffer, strlen(buffer));
			idv.set(oidv);
		}
	}
}

void MongoDBDataSourceImpl::getBSONObjectFromObject(const string& clasName, void* object, bson_t* b, const bool& isIdBsonAppend) {
	DataSourceEntityMapping dsemp = mapping->getDataSourceEntityMapping(clasName);
	string tableName = dsemp.getTableName();
	strMap clsprpmap = dsemp.getPropertyColumnMapping();
	map<string, string>::iterator clsprpmapit;
	ClassInfo clas = reflector->getClassInfo(clasName, appName);

	for(clsprpmapit=clsprpmap.begin();clsprpmapit!=clsprpmap.end();++clsprpmapit)
	{
		string prop = clsprpmapit->first;
		string col = clsprpmapit->second;
		Field pf = clas.getField(prop);
		args argus;
		vector<void *> valus;
		string methname = "get"+StringUtil::capitalizedCopy(prop);
		Method meth = clas.getMethod(methname,argus);

		//MongoDB has the _id attribute as the id for an GenericObject
		if(dsemp.getIdPropertyName()==prop) {
			col = "_id";
			if(dsemp.isIdGenerate() && !isIdBsonAppend) {
				continue;
			}
		}

		if(pf.getType()=="short" || pf.getType()=="int" || pf.getType()=="unsigned short")
		{
			long val = reflector->invokeMethod<long>(object,meth,valus);
			bson_append_int32(b, col.c_str(), col.length(), val);
		}
		else if(pf.getType()=="unsigned int" || pf.getType()=="long" || pf.getType()=="unsigned long" || pf.getType()=="long long")
		{
			long long val = reflector->invokeMethod<long long>(object,meth,valus);
			bson_append_int64(b, col.c_str(), col.length(), val);
		}
		else if(pf.getType()=="float" || pf.getType()=="double")
		{
			double val = reflector->invokeMethod<double>(object,meth,valus);
			bson_append_double(b, col.c_str(), col.length(), val);
		}
		else if(pf.getType()=="string" || pf.getType()=="std::string")
		{
			string val = reflector->invokeMethod<string>(object,meth,valus);
			bson_append_utf8(b, col.c_str(), col.length(), val.c_str(), val.length());
		}
		else if(pf.getType()=="bool")
		{
			bool val = reflector->invokeMethod<bool>(object,meth,valus);
			bson_append_bool(b, col.c_str(), col.length(), val);
		}
		else if(pf.getType().find("std::vector<short,")!=string::npos)
		{
			vector<short> *val = (vector<short>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_int32(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<unsigned short,")!=string::npos)
		{
			vector<unsigned short> *val = (vector<unsigned short>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_int32(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<int,")!=string::npos)
		{
			vector<int> *val = (vector<int>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_int32(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<unsigned int,")!=string::npos)
		{
			vector<unsigned int> *val = (vector<unsigned int>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_int32(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<long,")!=string::npos)
		{
			vector<long> *val = (vector<long>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_int32(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<unsigned long,")!=string::npos)
		{
			vector<unsigned long> *val = (vector<unsigned long>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_int64(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<long long,")!=string::npos)
		{
			vector<long long> *val = (vector<long long>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_int64(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<bool,")!=string::npos)
		{
			vector<bool> *val = (vector<bool>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_bool(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<float,")!=string::npos)
		{
			vector<float> *val = (vector<float>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_double(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<double,")!=string::npos)
		{
			vector<double> *val = (vector<double>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_double(b, "", 0, val->at(var));
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<std::string,")!=string::npos
				|| pf.getType().find("std::vector<string,")!=string::npos)
		{
			vector<string> *val = (vector<string>*)reflector->invokeMethodGVP(object,meth,valus);
			bson_t* child = bson_new();
			bson_append_array_begin(b, col.c_str(), col.length(), child);
			for (int var = 0; var < (int)val->size(); ++var) {
				bson_append_utf8(b, "", 0, val->at(var).c_str(), val->at(var).length());
			}
			bson_append_array_end(b, child);
			bson_destroy(child);
			delete val;
		}
		else if(pf.getType().find("std::vector<")!=string::npos)
		{
			string pclsnm = pf.getType();
			StringUtil::replaceFirst(pclsnm,"std::vector<","");
			string vtyp = pclsnm.substr(0,pclsnm.find(","));
			void *val = reflector->invokeMethodGVP(object,meth,valus);
			if(val!=NULL)
			{
				bson_t* child = bson_new();
				bson_append_array_begin(b, col.c_str(), col.length(), child);
				int contSize = reflector->getContainerSize(val, vtyp, "std::vector", appName);
				for (int var = 0; var < contSize; ++var) {
					void* contEle = reflector->getContainerElementAt(val, var, vtyp, "std::vector", appName);
					getBSONObjectFromObject(vtyp, contEle, b, true);
					delete contEle;
				}
				bson_append_array_end(b, child);
				bson_destroy(child);
				delete val;
			}
		}
		else
		{
			void* val = reflector->invokeMethodGVP(object,meth,valus);
			if(val!=NULL)
			{
				bson_t* child = bson_new();
				bson_append_document_begin(b, col.c_str(), col.length(), child);
				getBSONObjectFromObject(pf.getType(), val, b, true);
				bson_append_document_end(b, child);
				bson_destroy(child);
				delete val;
			}
		}
	}
}

long long getIterNumericVal(bson_iter_t& i, bson_type_t& t)
{
	long long v = 0;
	switch (t) {
		case BSON_TYPE_INT32:
		{
			v = bson_iter_int32(&i);
			break;
		}
		case BSON_TYPE_INT64:
		{
			v = bson_iter_int64(&i);
			break;
		}
		case BSON_TYPE_DOUBLE:
		{
			v = bson_iter_double(&i);
			break;
		}
		default:
			break;
	}
	return v;
}

void* MongoDBDataSourceImpl::getObject(bson_t* data, uint8_t* buf, uint32_t len, const string& clasName) {
    bson_iter_t i;
    string key;
    char oidhex[25];

    if(buf != NULL)
    {
    	data = bson_new();
    	bson_init_static(data, buf, len);
    	bson_iter_init(&i , data);
    }
	else
	{
		bson_iter_init(&i , data);
	}

    DataSourceEntityMapping dsemp = mapping->getDataSourceEntityMapping(clasName);

	ClassInfo clas = reflector->getClassInfo(clasName, appName);
    args argus1;
	Constructor ctor = clas.getConstructor(argus1);
	void *instance = reflector->newInstanceGVP(ctor);

    while ( bson_iter_next( &i ) ){
    	bson_type_t t = bson_iter_type( &i );
        if ( t == 0 )
            break;
        key = string(bson_iter_key(&i));

        string fieldName;

        if(key=="_id") {
        	fieldName = dsemp.getIdPropertyName();
        } else {
        	fieldName = dsemp.getPropertyForColumn(key);
        }

        Field fe = clas.getField(fieldName);

        //Every property should have a column mapping
        /*if(fe.getFieldName()=="") {
        	fe = clas.getField(key);
        }*/
        if(fe.getFieldName()=="")
        	continue;

        switch (t) {
			case BSON_TYPE_INT32:
			case BSON_TYPE_INT64:
			case BSON_TYPE_DOUBLE:
			{
				long long* d = new long long(getIterNumericVal(i, t));
				storeProperty(clas, instance, d, fe);
				delete d;
				break;
			}
			case BSON_TYPE_BOOL:
			{
				bool* b = new bool(bson_iter_bool(&i));
				storeProperty(clas, instance, b, fe);
				delete b;
				break;
			}
			case BSON_TYPE_UTF8:
			{
				uint32_t len;
				string* s = new string(bson_iter_utf8(&i, &len), len);
				storeProperty(clas, instance, s, fe);
				delete s;
				break;
			}
			case BSON_TYPE_NULL: break;
			case BSON_TYPE_OID:
			{
				bson_oid_to_string(bson_iter_oid(&i), oidhex);
				string* s = new string(oidhex);
				storeProperty(clas, instance, s, fe);
				delete s;
				break;
			}
			case BSON_TYPE_TIMESTAMP:
			{
				uint32_t inc;
				uint32_t ts;
				bson_iter_timestamp(&i, &ts, &inc);
				Date* dt = new Date;
				*dt = Date::getDateFromSeconds(ts);
				storeProperty(clas, instance, dt, fe);
				delete dt;
				break;
			}
			case BSON_TYPE_DATE_TIME:
			{
				int64_t td;
				td = bson_iter_date_time(&i);
				Date* dd = new Date;
				*dd = Date::getDateFromSeconds(td/1000);
				storeProperty(clas, instance, dd, fe);
				delete dd;
				break;
			}
			case BSON_TYPE_DOCUMENT:
			{
				void* ob = getObject(NULL, bson_iter_value(&i)->value.v_doc.data, bson_iter_value(&i)->value.v_doc.data_len,
						fe.getType());
				storeProperty(clas, instance, ob, fe);
				delete ob;
				break;
			}
			case BSON_TYPE_ARRAY:
			{
				string vtyp = fe.getType();

				StringUtil::replaceFirst(vtyp,"std::","");
				StringUtil::replaceFirst(vtyp,"vector<","");
				string te;

				if(vtyp.find(",")==string::npos) {
					te = vtyp.substr(0,vtyp.find(">"));
				} else {
					te = vtyp.substr(0,vtyp.find(","));
				}

				bson_iter_t ii;
				bson_value_t* abuf = (bson_value_t*)bson_iter_value(&i);
				bson_t* d = bson_new();
				bson_init_static(d, abuf->value.v_doc.data, abuf->value.v_doc.data_len);

				bson_iter_init(&ii , d);

				if(te=="short")
				{
					vector<short> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						long long v = getIterNumericVal(ii, t);
						veci.push_back((short)v);
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="unsigned short")
				{
					vector<unsigned short> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						long long v = getIterNumericVal(ii, t);
						veci.push_back((unsigned short)v);
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="int")
				{
					vector<int> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						long long v = getIterNumericVal(ii, t);
						veci.push_back((int)v);
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="unsigned int")
				{
					vector<unsigned int> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						long long v = getIterNumericVal(ii, t);
						veci.push_back((unsigned int)v);
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="long")
				{
					vector<long> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						long long v = getIterNumericVal(ii, t);
						veci.push_back((long)v);
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="unsigned long")
				{
					vector<unsigned long> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						long long v = getIterNumericVal(ii, t);
						veci.push_back((unsigned long)v);
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="long long")
				{
					vector<long long> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						long long v = getIterNumericVal(ii, t);
						veci.push_back(v);
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="float")
				{
					vector<float> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						long long v = getIterNumericVal(ii, t);
						veci.push_back((float)v);
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="double")
				{
					vector<double> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						long long v = getIterNumericVal(ii, t);
						veci.push_back((double)v);
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="string" || te=="std::string")
				{
					vector<string> veci;
					while ( bson_iter_next( &ii ) ){
						veci.push_back(string(bson_iter_utf8(&ii, &len), len));
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else if(te=="bool")
				{
					vector<bool> veci;
					while ( bson_iter_next( &ii ) ){
						bson_type_t t = bson_iter_type( &ii );
						veci.push_back(bson_iter_bool(&ii));
					}
					storeProperty(clas, instance, &veci, fe);
				}
				else
				{
					void* veci = reflector->getNewContainer(te, "std::vector", appName);
					if(veci!=NULL)
					{
						while ( bson_iter_next( &ii ) ){
							bson_type_t t = bson_iter_type( &ii );
							void* ob = getObject(NULL, bson_iter_value(&ii)->value.v_doc.data,
									bson_iter_value(&ii)->value.v_doc.data_len, te);
							reflector->addToContainer(veci, ob, te, "std::vector", appName);
							delete ob;
						}
						storeProperty(clas, instance, veci, fe);
					}
					else
					{
						logger << ("Invalid class definition found while deserializing "+clasName+" - invalid class " + te) << endl;
					}
				}
				bson_destroy(d);
				break;
			}
			default:
				break;
        }
    }
    if(buf != NULL)
    {
    	bson_destroy(data);
    }
    return instance;
}

void MongoDBDataSourceImpl::getMapOfProperties(bson_t* data, map<string, GenericObject>* map) {
    bson_iter_t i;
    string key;
    char oidhex[25];
    bson_iter_init( &i , data );

    while ( bson_iter_next( &i ) ) {
    	bson_type_t t = bson_iter_type( &i );
        if ( t == 0 )
            break;
        key = string(bson_iter_key(&i));

        switch (t) {
			case BSON_TYPE_INT32:
			{
				long d = bson_iter_int32(&i);
				(*map)[key].set(d);
				break;
			}
			case BSON_TYPE_INT64:
			{
				long long d = bson_iter_int64(&i);
				(*map)[key].set(d);
				break;
			}
			case BSON_TYPE_DOUBLE:
			{
				double db = bson_iter_double(&i);
				(*map)[key].set(db);
				break;
			}
			case BSON_TYPE_BOOL:
			{
				bool b = bson_iter_bool(&i);
				(*map)[key].set(b);
				break;
			}
			case BSON_TYPE_UTF8:
			{
				uint32_t len;
				string s = string(bson_iter_utf8(&i, &len), len);
				(*map)[key].set(s);
				break;
			}
			case BSON_TYPE_NULL: break;
			case BSON_TYPE_OID:
			{
				bson_oid_to_string(bson_iter_oid(&i), oidhex);
				string s = string(oidhex);
				(*map)[key].set(s);
				break;
			}
			case BSON_TYPE_TIMESTAMP:
			{
				uint32_t ts;
				uint32_t inc;
				bson_iter_timestamp(&i, &ts, &inc);
				Date dt = Date::getDateFromSeconds(ts);
				(*map)[key].set(dt);
				break;
			}
			case BSON_TYPE_DATE_TIME:
			{
				int64_t td;
				td = bson_iter_date_time(&i);
				Date dd = Date::getDateFromSeconds(td/1000);
				(*map)[key].set(dd);
				break;
			}
			case BSON_TYPE_DOCUMENT:
			case BSON_TYPE_ARRAY:
				break;
			default:
				break;
        }
    }
}

QueryComponent::~QueryComponent() {
	if(andChildren.size()>0)
	{
		for (QueryComponent* subQ : andChildren) {
			delete subQ;
		}
	}
	if(orChildren.size()>0)
	{
		for (QueryComponent* subQ : orChildren) {
			delete subQ;
		}
	}
}

bool MongoDBDataSourceImpl::startTransaction() {
	return false;
}

bool MongoDBDataSourceImpl::commit() {
	return false;
}

bool MongoDBDataSourceImpl::rollback() {
	return false;
}

void MongoDBDataSourceImpl::procedureCall(const string& procName) {
	throw "Not Implemented";
}

void MongoDBDataSourceImpl::empty(const string& clasName) {
	string collectionName = mapping->getTableForClass(clasName);
	string qstr = "db."+collectionName+".remove({})";
	Query q(qstr, clasName);
	executeUpdate(q);
}

long MongoDBDataSourceImpl::getNumRows(const string& clasName) {
	string collectionName = mapping->getTableForClass(clasName);
	string qstr = "db."+collectionName+".count({})";
	vector<map<string, GenericObject> > vec;
	Query q(qstr, clasName);
	void* count = executeQuery(q, false);
	if(count!=NULL) {
		long* d = (long*)count;
		long ld = *d;
		delete d;
		return ld;
	}
	return -1;
}

bool MongoDBDataSourceImpl::executeUpdate(Query& query) {
	bson_t* data = NULL;
	bson_t* q = NULL;
	string operationName;
	string collectionName = initializeDMLQueryParts(query, &data, &q, operationName);
	if(q==NULL) {
		q = bson_new();
	}
	if(operationName=="insert") {
		Connection* conn = _conn();
		mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
		bson_error_t er;
		bool fl = mongoc_collection_insert(collection, MONGOC_INSERT_NONE, data, NULL, &er);
		if(data==NULL)bson_destroy(data);
		_release(conn, collection);
		return fl;
	} else if(operationName=="save") {
		Connection* conn = _conn();
		mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
		bson_error_t er;
		bool fl = mongoc_collection_save(collection, data, NULL, &er);
		if(data==NULL)bson_destroy(data);
		bson_destroy(q);
		_release(conn, collection);
		return fl;
	} else if(operationName=="update") {
		Connection* conn = _conn();
		mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
		bson_error_t er;
		bool fl = mongoc_collection_update(collection, MONGOC_UPDATE_NONE, q, data, NULL, &er);
		if(data==NULL)bson_destroy(data);
		bson_destroy(q);
		_release(conn, collection);
		return fl;
	} else if(operationName=="updateMulti") {
		Connection* conn = _conn();
		mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
		bson_error_t er;
		bool fl = mongoc_collection_update(collection, MONGOC_UPDATE_MULTI_UPDATE, q, data, NULL, &er);
		if(data==NULL)bson_destroy(data);
		bson_destroy(q);
		_release(conn, collection);
		return fl;
	} else if(operationName=="remove") {
		Connection* conn = _conn();
		mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
		bson_error_t er;
		bool fl = mongoc_collection_remove(collection, MONGOC_REMOVE_NONE, q, NULL, &er);
		bson_destroy(q);
		_release(conn, collection);
		return fl;
	}
	return false;
}

vector<map<string, GenericObject> > MongoDBDataSourceImpl::execute(QueryBuilder& qb) {
	void* res = executeQuery(qb, false);
	vector<map<string, GenericObject> > vec;
	if(res!=NULL) {
		vec = *(vector<map<string, GenericObject> >*)res;
		delete res;
	}
	return vec;
}

bool MongoDBDataSourceImpl::executeInsert(Query& query, void* entity) {
	bool fl = false;
	bson_t* data = bson_new();
	getBSONObjectFromObject(query.getClassName(), entity, data, true);

	DataSourceEntityMapping dsemp = mapping->getDataSourceEntityMapping(query.getClassName());
	ClassInfo clas = reflector->getClassInfo(query.getClassName(), appName);

	bson_iter_t i;
	bson_iter_init(&i, data);
	bool isIdFound = bson_iter_find(&i, "_id");

	if(isIdFound)
	{
		cout << bson_as_json(data, NULL) << endl;
		string collectionName = dsemp.getTableName();
		Connection* conn = _conn();
		mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
		bson_error_t er;
		fl = mongoc_collection_insert(collection, MONGOC_INSERT_NONE, data, NULL, &er);
		_release(conn, collection);
	}
	bson_destroy(data);
	return fl;
}

bool MongoDBDataSourceImpl::isGetDbEntityForBulkInsert() {
	return true;
}

void* MongoDBDataSourceImpl::getDbEntityForBulkInsert(void* entity, const string& clasName, string& error) {
	bson_t* b = bson_new();
	getBSONObjectFromObject(clasName, entity, b, true);
	return b;
}

//mongoc_collection_insert_bulk is deprecated
/*bool MongoDBDataSourceImpl::executeInsertBulk(Query& query, vector<void*> entities, vector<void*> dbEntities) {
	string collectionName = mapping->getTableForClass(query.getClassName());
	bson_t** data;
	data = new bson_t*[dbEntities.size()];
	for (int k = 0; k < (int)dbEntities.size(); k++) {
		data[k] = (bson_t*)dbEntities.at(k);
	}
	Connection* conn = _conn();
	mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
	bson_error_t er;
	bool fl = mongoc_collection_insert_bulk(collection, MONGOC_INSERT_NONE, (const bson_t**)data, (int)dbEntities.size(), NULL, &er);
	_release(conn, collection);
	for (int k = 0; k < (int)dbEntities.size(); k++) {
		bson_destroy(data[k]);
	}
	delete[] data;
	return fl;
}*/

bool MongoDBDataSourceImpl::executeInsertBulk(Query& query, vector<void*> entities, vector<void*> dbEntities) {
	string collectionName = mapping->getTableForClass(query.getClassName());
	Connection* conn = _conn();

	mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
	mongoc_bulk_operation_t* bulk = mongoc_collection_create_bulk_operation (collection, true, NULL);
	for (int k = 0; k < (int)dbEntities.size(); k++) {
		mongoc_bulk_operation_insert (bulk, (bson_t*)dbEntities.at(k));
		bson_destroy((bson_t*)dbEntities.at(k));
	}
	bson_t reply;
	bson_error_t error;
	bool fl = mongoc_bulk_operation_execute (bulk, &reply, &error);
	mongoc_bulk_operation_destroy (bulk);
	_release(conn, collection);
	return fl;
}

bool MongoDBDataSourceImpl::executeUpdateBulk(Query& query, vector<void*> entities, vector<void*> dbEntities) {
	string collectionName = mapping->getTableForClass(query.getClassName());
	Connection* conn = _conn();

	bool fl = true;
	mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
	mongoc_bulk_operation_t* bulk = mongoc_collection_create_bulk_operation (collection, true, NULL);
	for (int k = 0; k < (int)dbEntities.size(); k++) {
		bson_error_t er;
		fl &= mongoc_collection_save(collection, (bson_t*)dbEntities.at(k), NULL, &er);
		bson_destroy((bson_t*)dbEntities.at(k));
	}
	_release(conn, collection);
	return fl;
}

bool MongoDBDataSourceImpl::executeUpdate(Query& query, void* entity) {
	bool fl = false;
	bson_t* data = bson_new();
	getBSONObjectFromObject(query.getClassName(), entity, data, true);

	bson_iter_t i;
	bson_iter_init(&i, data);
	bool isIdFound = bson_iter_find(&i, "_id");

	DataSourceEntityMapping dsemp = mapping->getDataSourceEntityMapping(query.getClassName());
	if(isIdFound)
	{
		string collectionName = dsemp.getTableName();
		Connection* conn = _conn();
		mongoc_collection_t *collection = _collection (conn, collectionName.c_str());
		bson_error_t er;
		fl = mongoc_collection_save(collection, data, NULL, &er);
		_release(conn, collection);
	}
	bson_destroy(data);
	return fl;
}

bool MongoDBDataSourceImpl::remove(const string& clasName, GenericObject& id) {
	string idClsName = id.getTypeName();
	string collectionName = mapping->getTableForClass(clasName);
	string idValue = "";
	if(id.isNumber() || id.isFPN())
	{
		idValue = id.getSerilaizedState();
	}
	else if(id.isString())
	{
		idValue = "\"" + id.getSerilaizedState() + "\"";
	}

	string qstr = string("db."+collectionName+".remove({\"$query\": {\"_id\":") + idValue + string("}})");
	Query q(qstr, clasName);
	return executeUpdate(q);
}

void* MongoDBDataSourceImpl::executeQuery(Query& cquery, const bool& isObj) {
	bson_t* querySpec = bson_new();
	bson_t* fields = NULL;
	string operationName;
	string collectionName;
	bool isCountQuery = false;
	if(!isObj)
	{
		collectionName = initializeQueryParts(cquery, &querySpec, &fields, operationName);
		if(operationName=="findOne") {
			cquery.setCount(1);
			cquery.setStart(0);
		} else if(operationName=="count") {
			if(fields!=NULL) {
				bson_destroy(querySpec);
				bson_destroy(fields);
				return new double(-1);
			}
			isCountQuery = true;
		} else if(operationName!="find") {
			bson_destroy(querySpec);
			if(fields!=NULL)bson_destroy(fields);
			return NULL;
		}
	}
	else
	{
		DataSourceEntityMapping dsemp = mapping->getDataSourceEntityMapping(cquery.getClassName());
		collectionName = dsemp.getTableName();
	}
	return getResults(collectionName, cquery, querySpec, fields, isObj, isCountQuery);
}

void* MongoDBDataSourceImpl::executeQuery(QueryBuilder& qb, const bool& isObj) {
	bson_t* fields = NULL;
	bson_t* query = NULL;

	string collectionName = qb.getTableName();
	bool isClassProps = false;
	DataSourceEntityMapping dsemp;

	if(qb.getClassName()!="") {
		dsemp = mapping->getDataSourceEntityMapping(qb.getClassName());
		collectionName = dsemp.getTableName();
		isClassProps = true;
	}

	if(!qb.isAllCols())
	{
		fields = NULL;
		map<string, string> cols = qb.getColumns();
		if(cols.size()>0)
		{
			fields = bson_new();
		}
		map<string, string>::iterator it;
		for (it=cols.begin(); it!=cols.end(); ++it) {
			if(!isClassProps)
			{
				bson_append_int32(fields, it->first.c_str(), it->first.length(), 1);
			}
			else
			{
				if(dsemp.getColumnForProperty(it->first)!="")
				{
					bson_append_int32(fields, dsemp.getColumnForProperty(it->first).c_str(),
							dsemp.getColumnForProperty(it->first).length(), 1);
				}
			}
		}
	}

	if(qb.getConditions().getConds().size()>0) {
		QueryComponent* sq = getQueryComponent(qb.getConditions().getConds());
		populateQueryComponents(sq);
		query = sq->actualQuery;
		delete sq;
	}
	if(query==NULL) {
		query = bson_new();
	}
	return getResults(collectionName, qb, query, fields, isObj);
}

Connection* MongoDBDataSourceImpl::_conn() {
	if(context==NULL) {
		return pool->checkout();
	} else {
		return ((MongoContext*)context)->conn;
	}
}

mongoc_collection_t* MongoDBDataSourceImpl::_collection(Connection* conn, const char* collName) {
	if(context==NULL) {
		return mongoc_client_get_collection ((mongoc_client_t*)conn->getConn(), conn->getNode().getDatabaseName().c_str(), collName);
	} else {
		if(((MongoContext*)context)->collection==NULL) {
			((MongoContext*)context)->collection = mongoc_client_get_collection ((mongoc_client_t*)conn->getConn(), conn->getNode().getDatabaseName().c_str(), collName);
		}
		return ((MongoContext*)context)->collection;
	}
}

void MongoDBDataSourceImpl::_release(Connection* conn, mongoc_collection_t* collection) {
	if(context==NULL) {
		mongoc_collection_destroy (collection);
		pool->release(conn);
	}
}

void* MongoDBDataSourceImpl::getContext(void* details) {
	MongoContext* mc = new MongoContext;
	string* mcd = (string*)details;
	mc->conn = pool->checkout();
	if(mcd!=NULL && *mcd!="") {
		mc->collection = mongoc_client_get_collection ((mongoc_client_t*)mc->conn->getConn(), mc->conn->getNode().getDatabaseName().c_str(), mcd->c_str());
	}
	return mc;
}

void MongoDBDataSourceImpl::destroyContext(void* cntxt) {
	if(cntxt==NULL)return;
	MongoContext* mc = (MongoContext*)cntxt;
	if(mc->collection!=NULL) {
		mongoc_collection_destroy (mc->collection);
	}
	pool->release(mc->conn);
	delete mc;
}

MongoContext::MongoContext() {
	collection = NULL;
	conn = NULL;
}

MongoContext::~MongoContext() {
}
