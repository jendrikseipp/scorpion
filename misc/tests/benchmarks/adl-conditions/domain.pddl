;; Small ADL domain whose conditions exercise the translator's condition
;; normalization: disjunctive preconditions, existential and universal
;; quantification, a disjunctive effect condition and a disjunctive goal.
;; The three --condition-normalization-strategy values produce noticeably
;; different SAS+ tasks for it.
(define (domain adl-conditions)
  (:requirements :adl :typing)
  (:types loc obj - object)
  (:predicates
    (at ?o - obj ?l - loc)
    (linked ?l1 - loc ?l2 - loc)
    (marked ?l - loc)
    (blocked ?l - loc)
    (done ?o - obj))

  (:action move
    :parameters (?o - obj ?from - loc ?to - loc)
    :precondition (and (at ?o ?from)
                       (or (linked ?from ?to) (linked ?to ?from))
                       (not (blocked ?to))
                       (exists (?l - loc) (and (marked ?l) (linked ?l ?to))))
    :effect (and (not (at ?o ?from)) (at ?o ?to)))

  (:action finish
    :parameters (?o - obj)
    :precondition (or (exists (?l - loc) (and (at ?o ?l) (marked ?l)))
                      (forall (?l - loc) (not (blocked ?l))))
    :effect (done ?o))

  (:action mark
    :parameters (?l - loc)
    :precondition (not (marked ?l))
    :effect (when (or (blocked ?l) (exists (?o - obj) (at ?o ?l)))
                  (marked ?l)))
)
