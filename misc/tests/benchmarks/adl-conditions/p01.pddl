(define (problem adl-conditions-p01)
  (:domain adl-conditions)
  (:objects
    l1 l2 l3 l4 - loc
    o1 o2 - obj)
  (:init
    (at o1 l1)
    (at o2 l2)
    (linked l1 l2)
    (linked l2 l3)
    (linked l3 l4)
    (marked l1)
    (blocked l4))
  (:goal (and (done o1)
              (exists (?l - loc) (and (at o1 ?l) (marked ?l)))
              (or (at o1 l3) (forall (?o - obj) (done ?o))))))
