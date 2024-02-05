#include "Binder_Cursor.hxx"
#include "Binder_Common.hxx"
#include "Binder_Util.hxx"

#include <algorithm>
#include <iostream>
#include <map>
#include <set>

Binder_Cursor::Binder_Cursor(const CXCursor &theCursor) : myCursor(theCursor) {}

Binder_Cursor::~Binder_Cursor() {}

std::string Binder_Cursor::Spelling() const {
  return Binder_Util_GetCString(clang_getCursorSpelling(myCursor));
}

bool Binder_Cursor::IsTransient() const {
  if (Spelling() == "Standard_Transient")
    return true;

  std::vector<Binder_Cursor> aBases = GetAllBases();

  for (const auto &aBase : aBases) {
    if (aBase.Type().Spelling() == "Standard_Transient")
      return true;
  }

  return false;
}

bool Binder_Cursor::IsOperator() const {
  if (IsFunction() || IsCxxMethod()) {
    return Binder_Util_Contains(binder::LUA_OPERATORS, Spelling());
  }

  return false;
}

bool Binder_Cursor::IsGetterMethod() const {
  if (IsCxxMethod() && IsPublic() && ReturnType().IsLvalue()) {
    std::string aSetterName = "Set" + Spelling();

    for (const auto &aMethod : Parent().Methods()) {
      if (aMethod.Spelling() == aSetterName && aMethod.IsPublic())
        return true;
    }
  }

  return false;
}

bool Binder_Cursor::IsImmutable() const {
  Binder_Type aType = Type();

  if (aType.IsPointerLike())
    aType = aType.GetPointee();

  Binder_Type originType =
      clang_getTypedefDeclUnderlyingType(aType.GetDeclaration());

  if (Binder_Util_Contains(binder::IMMUTABLE_TYPE,
                           aType.GetDeclaration().Spelling()) ||
      Binder_Util_Contains(binder::IMMUTABLE_TYPE,
                           originType.GetDeclaration().Spelling()) ||
      aType.GetDeclaration().IsEnum())
    return true;

  return false;
}

bool Binder_Cursor::IsInlined() const {
  return clang_Cursor_isFunctionInlined(myCursor);
}

bool Binder_Cursor::NeedsInOutMethod() const {
  for (auto &p : Parameters()) {
    Binder_Type aType = p.Type();

    if (!aType.IsPointerLike())
      continue;

    aType = aType.GetPointee();

    if (p.IsImmutable() && !aType.IsConstQualified()) {
      return true;
    }
  }

  return false;
}

void Binder_Cursor::GetInOutParams(std::vector<Binder_Cursor> &theIn,
                                   std::vector<Binder_Cursor> &theOut) const {
  for (auto &p : Parameters()) {
    Binder_Type aType = p.Type();

    if (aType.IsPointerLike()) {
      aType = aType.GetPointee();

      if (p.IsImmutable() && !aType.IsConstQualified()) {
        theOut.push_back(p);
        continue;
      }
    }

    theIn.push_back(p);
  }
}

bool Binder_Cursor::NeedsDefaultCtor() const {
  if (IsAbstract())
    return false;

  if (!Ctors().empty())
    return false;

  for (const auto &anItem : GetAllBases()) {
    if (!anItem.GetDefinition().Ctors().empty())
      return false;
  }

  return true;
}

static void getBases(const Binder_Cursor &theCursor,
                     std::vector<Binder_Cursor> &theBases) {
  for (const auto aBase : theCursor.Bases()) {
    theBases.push_back(aBase);
    Binder_Cursor aDecl = aBase.Type().GetDeclaration();

    if (aDecl.NoDecl())
      continue;

    if (aDecl.IsClassTemplate()) {
      getBases(aDecl, theBases);
      continue;
    }

    Binder_Cursor aSepc = aDecl.GetSpecialization();

    if (!aSepc.NoDecl() && aSepc.IsClassTemplate()) {
      getBases(aSepc, theBases);
      continue;
    }

    if (aDecl.IsClass()) {
      getBases(aDecl, theBases);
      continue;
    }

    if (aDecl.IsTypeDef()) {
      aDecl = aDecl.UnderlyingTypedefType().GetDeclaration();
      aSepc = aDecl.GetSpecialization();

      if (!aSepc.NoDecl() && aSepc.IsClassTemplate()) {
        getBases(aSepc, theBases);
      } else {
        getBases(aDecl, theBases);
      }

      continue;
    }

    std::cout << "Failed to find a base for " << aBase.Spelling() << '\n';
  }
}

std::vector<Binder_Cursor> Binder_Cursor::GetAllBases() const {
  std::vector<Binder_Cursor> aBases{};
  Binder_Cursor aSpec = GetSpecialization();

  if (!aSpec.NoDecl() && aSpec.IsClassTemplate()) {
    getBases(aSpec, aBases);
  } else {
    getBases(*this, aBases);
  }

  return aBases;
}

std::vector<Binder_Cursor> Binder_Cursor::GetChildren() const {
  std::vector<Binder_Cursor> aChildren{};

  clang_visitChildren(
      myCursor,
      [](CXCursor theCursor, CXCursor theParent, CXClientData theClientData) {
        auto aChildrenPtr =
            static_cast<std::vector<Binder_Cursor> *>(theClientData);
        aChildrenPtr->push_back(theCursor);

        return CXChildVisit_Continue;
      },
      &aChildren);

  return aChildren;
}

std::vector<Binder_Cursor>
Binder_Cursor::GetChildrenOfKind(CXCursorKind theKind,
                                 bool thePublicOnly) const {
  std::vector<Binder_Cursor> aChildren = GetChildren();
  std::vector<Binder_Cursor> aChildrenQualified{};

  std::copy_if(aChildren.cbegin(), aChildren.cend(),
               std::back_inserter(aChildrenQualified),
               [=](const Binder_Cursor &theCursor) {
                 if (theCursor.Kind() != theKind)
                   return false;

                 if (thePublicOnly && !theCursor.IsPublic())
                   return false;

                 return true;
               });

  return aChildrenQualified;
}

std::string Binder_Cursor::Docs() const {
  return Binder_Util_GetCString(clang_Cursor_getBriefCommentText(myCursor));
}

bool Binder_Cursor::IsStaticClass() const {
  int nbStatic = 0;

  for (const auto &aMethod : GetChildrenOfKind(CXCursor_CXXMethod, true)) {
    // Operators is static, too...
    if (Binder_Util_StartsWith(aMethod.Spelling(), "operator"))
      continue;

    if (aMethod.IsStaticMethod()) {
      nbStatic++;
    } else {
      return false;
    }
  }

  return (nbStatic > 0);
}
